/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "StandardDrawOp.h"
#include "core/utils/Log.h"
#include "gpu/AOTChainBuilder.h"
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/AOTPlanExecutor.h"
#include "gpu/AOTRouter.h"
#include "gpu/GlobalCache.h"
#include "gpu/PermutationMatcher.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/Program.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ConstColorProcessor.h"

namespace tgfx {

static bool ContainsGradientSource(const FragmentProcessor* processor) {
  if (processor == nullptr) {
    return false;
  }
  if (IsProcessor<ClampedGradientEffect>(processor)) {
    return true;
  }
  for (size_t index = 0; index < processor->numChildProcessors(); ++index) {
    if (ContainsGradientSource(processor->childProcessor(index))) {
      return true;
    }
  }
  return false;
}

void StandardDrawOp::execute(RenderPass* renderPass, RenderTarget* renderTarget) {
  if (!onPrepare()) {
    return;
  }
  if (!prepare(renderTarget, ProgramLookupMode::AllowRuntimeFallback, std::nullopt,
               renderPass->depthStencilFormat())) {
    return;
  }
  executePrepared(renderPass, renderTarget);
}

bool StandardDrawOp::prepare(RenderTarget* renderTarget, ProgramLookupMode mode,
                             std::optional<ColorProcessorList> colorOverride,
                             PixelFormat depthStencilFormat) {
  if (!geometryProcessorInitialized) {
    geometryProcessorInitialized = true;
    geometryProcessor = onMakeGeometryProcessor(renderTarget);
  }
  if (geometryProcessor == nullptr) {
    return false;
  }

  preparedProgram = nullptr;
  preparedProgramInfo = nullptr;
  preparedRenderTarget = nullptr;
  preparedColors = std::move(colorOverride);
  auto& activeColors = preparedColors.has_value() ? *preparedColors : colors;

  std::vector<FragmentProcessor*> fragmentProcessors = {};
  fragmentProcessors.reserve(activeColors.size() + coverages.size());
  for (auto& color : activeColors) {
    fragmentProcessors.emplace_back(color.get());
  }
  for (auto& coverage : coverages) {
    fragmentProcessors.emplace_back(coverage.get());
  }
  preparedProgramInfo = std::make_unique<ProgramInfo>(
      renderTarget, geometryProcessor.get(), std::move(fragmentProcessors), activeColors.size(),
      xferProcessor.get(), blendMode);
  preparedProgramInfo->setCullMode(cullMode);
  if (depthStencilFormat != PixelFormat::Unknown) {
    // Some backends reject a pipeline that declares no depth-stencil format while the pass has a
    // depth-stencil attachment. Inherit the pass format; the default no-op depth/stencil state
    // keeps the draw behaviour unchanged.
    DepthStencilDescriptor depthStencil = {};
    depthStencil.format = depthStencilFormat;
    preparedProgramInfo->setDepthStencil(depthStencil);
  }
  onConfigureProgramInfo(*preparedProgramInfo);
  bool interpreterServed = false;
  preparedProgram = prepareDecomposedProgram(renderTarget, activeColors, &interpreterServed);
  preparedViaDecomposition = preparedProgram != nullptr || interpreterServed;
  if (preparedProgram == nullptr) {
    preparedProgram = preparedProgramInfo->getProgram(mode);
  }
  if (preparedProgram == nullptr) {
    if (mode == ProgramLookupMode::AllowRuntimeFallback) {
      // The final lookup for this draw (PrecompiledOnly lookups are AOT plan probes, which fall
      // back to the original draw): nothing will be drawn, so the drop must be visible.
      LOGE("StandardDrawOp::prepare() No program for the draw; the draw is dropped!");
      auto cache = renderTarget->getContext()->precompiledShaderCache();
      if (cache->diagnosticRecordingEnabled()) {
        cache->recordDroppedDraw();
      }
    }
    return false;
  }
  preparedRenderTarget = renderTarget;
  return true;
}

// When the plain matcher cannot serve the color chain, the chain may still reduce onto the fused
// pointwise-chain kernel. Rewriting the processors before program lookup keeps the cached program
// and the per-draw uniform upload driven by the same ProgramInfo, which a post-hoc program swap
// cannot guarantee. The matcher probe runs first so draws the plain route already serves keep
// their original funnel accounting. A single coverage FP is folded into the chain (an AA rect clip
// as a coverage slot, a device-space alpha mask as the mask child, Compose(mask, rect) as both);
// other coverage forms stay on their original route.
std::shared_ptr<Program> StandardDrawOp::prepareDecomposedProgram(
    RenderTarget* renderTarget, const ColorProcessorList& activeColors, bool* interpreterServed) {
  auto cache = renderTarget->getContext()->precompiledShaderCache();
  if (!cache->isLoaded()) {
    return nullptr;
  }
  if (!cache->decompositionEnabled()) {
    bool requiresGradientLowering = false;
    for (size_t index = 0; index < preparedProgramInfo->numFragmentProcessors(); ++index) {
      if (ContainsGradientSource(preparedProgramInfo->getFragmentProcessor(index))) {
        requiresGradientLowering = true;
        break;
      }
    }
    if (!requiresGradientLowering) {
      return nullptr;
    }
  }
  if (cache->dedicatedKernelsEnabled() && !isInlineRewriteDeferred()) {
    auto cached =
        renderTarget->getContext()->globalCache()->findProgram(preparedProgramInfo->programKey());
    if (cached != nullptr &&
        cached->getProvenance().program == ProgramOrigin::PrecompiledArtifact) {
      // The program cache's key-to-program premise: this exact key already resolved to a
      // precompiled artifact, so the match that produced it succeeded and the plain route serves
      // the draw — the probe below would only repeat that walk.
      DEBUG_ASSERT(MatchPermutation(preparedProgramInfo.get()).has_value());
      if (interpreterServed != nullptr && cached->shaderName() == "PointwiseChainShader") {
        *interpreterServed = true;
      }
      return nullptr;
    }
    auto probe = MatchPermutation(preparedProgramInfo.get());
    if (probe.has_value()) {
      // The interpreter serves this draw through the plain route (its Extract re-expresses the
      // draw's own tree), so the rewrite has nothing left to own: the direct-chain accounting
      // follows the plain route instead.
      if (interpreterServed != nullptr && probe->shaderName == "PointwiseChainShader") {
        *interpreterServed = true;
      }
      return nullptr;
    }
  }
  // The coverage forms the chain carries are now semantically routed: geometric coverage (the
  // narrow clip slots, the device mask, the atlas glyph subtree via the clip-coverage register,
  // analytic subtrees via CoverageRootIndex) composites as the XP's coverage input, while a
  // MaskFilter's source-modulating mask subtree keeps the CoverageRootIndex source route — so no
  // blend-mode restriction remains here (the interim blend-mode guards are gone; see
  // NarrowAAClipWithSrcBlendKeepsDstOnEdge and AtlasTextNonSrcOverBlendKeepsDstProbe).
  std::vector<const FragmentProcessor*> coverageFPs = {};
  std::vector<PlacementPtr<FragmentProcessor>> ownedChainCoverageFPs = {};
  // Atlas text: the GP-owned atlas becomes a synthesized coverage leaf (the glyph mask), and the
  // rewrite needs a sampler-free twin GP. A null override means the draw is not servable this way
  // (e.g. a color-emoji atlas), so keep the original route. The glyph mask is TRUE coverage in
  // the runtime composite, and the builder routes the subtree's root through the chain's
  // clip-coverage register so it composites as the XP's coverage input for every blend mode
  // (AtlasTextNonSrcOverBlendKeepsDstProbe) — no blend-mode restriction here. A draw clip rides
  // in front of the glyph leaf as the two-FP [clip, local texture] form; the builder chains the
  // clip slots on top of the glyph register (glyph * clip).
  const bool atlasText = IsProcessor<AtlasTextGeometryProcessor>(geometryProcessor.get());
  // Up to two coverage FPs fold into the chain: a single one lowers as the narrow forms or a
  // general subtree, and a pair folds as subtree + clip slots or device-mask child. The atlas
  // glyph leaf takes one of the two positions.
  if (coverages.size() > (atlasText ? 1u : 2u)) {
    return nullptr;
  }
  coverageFPs.reserve(coverages.size() + 1);
  for (auto& coverage : coverages) {
    coverageFPs.push_back(coverage.get());
  }
  if (atlasText) {
    chainGeometryProcessor = onMakeChainGeometryProcessor(&ownedChainCoverageFPs);
    if (chainGeometryProcessor == nullptr) {
      return nullptr;
    }
    for (auto& fp : ownedChainCoverageFPs) {
      coverageFPs.push_back(fp.get());
    }
  }
  std::vector<const FragmentProcessor*> colorProcessors = {};
  colorProcessors.reserve(activeColors.size() + 1);
  for (auto& color : activeColors) {
    colorProcessors.push_back(color.get());
  }
  PlacementPtr<FragmentProcessor> unitColor = nullptr;
  if (colorProcessors.empty()) {
    // A color-free draw (pure mask/clip fill) takes the paint color as-is, which is a one-slot
    // const-color chain modulating the geometry color.
    unitColor = ConstColorProcessor::Make(allocator, PMColor::White(), InputMode::ModulateRGBA);
    if (unitColor == nullptr) {
      return nullptr;
    }
    colorProcessors.push_back(unitColor.get());
  }
  AOTEffectGraph graph = {};
  AOTEffectPlan plan = {};
  if (!AOTRouter::PlanInlineRewrite(colorProcessors, &graph, &plan)) {
    return nullptr;
  }
  auto kernel = plan.passes[0].kernel;
  PlacementPtr<FragmentProcessor> chainFP = nullptr;
  if (kernel == AOTKernelKind::PerlinNoiseFill) {
    // The perlin kernel carries its own coordinate transform and has no coverage-fold path.
    if (!coverageFPs.empty()) {
      return nullptr;
    }
    chainFP = AOTChainBuilder::BuildFPForPass(allocator, graph, plan.passes[0]);
  } else {
    chainFP = AOTChainBuilder::BuildChainProcessor(allocator, graph, plan.passes[0], coverageFPs,
                                                   !ownedChainCoverageFPs.empty());
  }
  if (chainFP == nullptr) {
    return nullptr;
  }
  std::vector<FragmentProcessor*> rewrittenProcessors = {chainFP.get()};
  auto* chainGP =
      chainGeometryProcessor != nullptr ? chainGeometryProcessor.get() : geometryProcessor.get();
  auto rewrittenInfo = std::make_unique<ProgramInfo>(
      renderTarget, chainGP, std::move(rewrittenProcessors), 1, xferProcessor.get(), blendMode);
  // The rewrite only changes the processor expression, never the other pipeline state, so every
  // non-processor state the original ProgramInfo carries over. A pass with a depth-stencil
  // attachment rejects pipelines that do not declare the format (see the inheritance note in
  // prepare()), and dropping the colour write mask would un-mask a write-disabled draw.
  rewrittenInfo->setCullMode(cullMode);
  rewrittenInfo->setDepthStencil(preparedProgramInfo->getDepthStencil());
  rewrittenInfo->setColorWriteMask(preparedProgramInfo->getColorWriteMask());
  // Probe before the strict lookup: a chain the matcher rejects (unsupported GP, perspective leaf
  // transforms) must fall back without recording a diagnostic miss against the rewritten tree,
  // which would double-count the draw alongside the original tree's own miss record. The cached
  // probe is what the strict lookup's own match reuses.
  if (!MatchPermutation(rewrittenInfo.get()).has_value()) {
    return nullptr;
  }
  // Strict lookup: a rewritten draw must resolve to the precompiled chain kernel. Anything else
  // (cache not loaded, missing artifact) falls back to the original processors.
  auto rewrittenProgram = rewrittenInfo->getProgram(ProgramLookupMode::PrecompiledOnly);
  if (rewrittenProgram == nullptr) {
    return nullptr;
  }
  preparedColors = ColorProcessorList{};
  preparedColors->emplace_back(std::move(chainFP));
  preparedProgramInfo = std::move(rewrittenInfo);
  return rewrittenProgram;
}

bool StandardDrawOp::prepareForTask(RenderTarget* renderTarget, ProgramLookupMode mode,
                                    std::optional<ColorProcessorList> colorOverride) {
  // Mirror execute()'s ordering: onPrepare() may veto the op (empty asynchronous buffers);
  // skipping it here would leave those ops drawing from null resources ().
  if (!onPrepare()) {
    return false;
  }
  return prepare(renderTarget, mode, std::move(colorOverride));
}

bool StandardDrawOp::executePrepared(RenderPass* renderPass, RenderTarget* renderTarget,
                                     bool recordDrawStats) {
  if (preparedProgramInfo == nullptr || preparedProgram == nullptr ||
      preparedRenderTarget == nullptr) {
    LOGE("StandardDrawOp::executePrepared() DrawOp is not prepared!");
    return false;
  }
  renderPass->setPipeline(preparedProgram->getPipeline());
  if (!preparedProgramInfo->setUniformsAndSamplers(renderPass, preparedProgram.get())) {
    // No uniform backing store: submitting would draw with unwritten (or garbage) uniforms.
    return false;
  }
  if (offscreenFillKey != InvalidOffscreenFillKey) {
    auto cache = preparedRenderTarget->getContext()->precompiledShaderCache();
    cache->recordOffscreenFillProgram(offscreenFillKey, preparedProgram->getProvenance().program);
  }
  applyScissor(renderPass, renderTarget);
  onDraw(renderPass, renderTarget);

  auto cache = preparedRenderTarget->getContext()->precompiledShaderCache();
  if (recordDrawStats && cache->diagnosticRecordingEnabled()) {
    AOTDrawStats drawDelta = {};
    drawDelta.kernelInvocations = 1;
    if (preparedViaDecomposition) {
      drawDelta.directChainDraws = 1;
    }
    bool completeAOTDraw =
        preparedProgram->getProvenance().program == ProgramOrigin::PrecompiledArtifact;
    cache->recordDraw(drawDelta, completeAOTDraw);
  }
  return true;
}
}  // namespace tgfx
