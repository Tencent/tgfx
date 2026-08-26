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
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/AOTPlanExecutor.h"
#include "gpu/PermutationMatcher.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/Program.h"
#include "gpu/processors/ConstColorProcessor.h"

namespace tgfx {
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
  preparedProgram = prepareDecomposedProgram(renderTarget, activeColors);
  if (preparedProgram == nullptr) {
    preparedProgram = preparedProgramInfo->getProgram(mode);
  }
  if (preparedProgram == nullptr) {
    LOGE("StandardDrawOp::prepare() Failed to get the program!");
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
    RenderTarget* renderTarget, const ColorProcessorList& activeColors) {
  if (!renderTarget->getContext()->precompiledShaderCache()->isLoaded()) {
    return nullptr;
  }
  if (MatchPermutation(preparedProgramInfo.get()).has_value()) {
    return nullptr;
  }
  std::vector<const FragmentProcessor*> coverageFPs = {};
  std::vector<PlacementPtr<FragmentProcessor>> ownedChainCoverageFPs = {};
  if (!coverages.empty()) {
    // Up to two coverage FPs fold into the chain: a single one lowers as the narrow forms or a
    // general subtree, and a pair folds as subtree + trailing device-mask child.
    if (coverages.size() > 2) {
      return nullptr;
    }
    coverageFPs.reserve(coverages.size());
    for (auto& coverage : coverages) {
      coverageFPs.push_back(coverage.get());
    }
  } else if (geometryProcessor->name() == "AtlasTextGeometryProcessor") {
    // Atlas text: the GP-owned atlas becomes a synthesized coverage leaf (the glyph mask), and
    // the rewrite needs a sampler-free twin GP. A null override means the draw is not servable
    // this way (e.g. a color-emoji atlas), so keep the original route.
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
  bool decomposed = AOTEffectDecomposer::Lower(colorProcessors, &graph) &&
                    AOTEffectDecomposer::ValidateForFusion(graph) &&
                    AOTEffectDecomposer::Decompose(graph, &plan);
  // A single-pass pointwise-tail plan is a linear texture-plus-ops chain, and the tail ops are a
  // subset of the chain op set, so the chain kernel can evaluate it as-is. This route only runs
  // after the plain matcher missed, which includes the shapes the tail rule rejects (no UV
  // matrix, alpha-only source, per-quad subset); rewriting the kernel field lets them resolve to
  // the chain artifact instead of falling back to runtime compilation. Device-space sources and
  // other chain-incompatible shapes are still rejected by CanExecute/BuildChainProcessor below.
  if (decomposed && plan.passes.size() == 1 &&
      plan.passes[0].kernel == AOTKernelKind::PointwiseTail) {
    plan.passes[0].kernel = AOTKernelKind::PointwiseChain;
  }
  auto kernel = plan.passes.empty() ? AOTKernelKind::TextureFill : plan.passes[0].kernel;
  if (!decomposed || plan.passes.size() != 1 ||
      (kernel != AOTKernelKind::PointwiseChain && kernel != AOTKernelKind::PerlinNoiseFill) ||
      !AOTPlanExecutor::CanExecute(graph, plan)) {
    return nullptr;
  }
  PlacementPtr<FragmentProcessor> chainFP = nullptr;
  if (kernel == AOTKernelKind::PerlinNoiseFill) {
    // The perlin kernel carries its own coordinate transform and has no coverage-fold path.
    if (!coverageFPs.empty()) {
      return nullptr;
    }
    chainFP = AOTPlanExecutor::BuildPerlinNoiseFP(allocator, graph, plan.passes[0]);
  } else {
    chainFP = AOTPlanExecutor::BuildChainProcessor(allocator, graph, plan.passes[0], coverageFPs,
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
  rewrittenInfo->setCullMode(cullMode);
  // Probe before the strict lookup: a chain the matcher rejects (unsupported GP, perspective leaf
  // transforms) must fall back without recording a diagnostic miss against the rewritten tree,
  // which would double-count the draw alongside the original tree's own miss record.
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

void StandardDrawOp::executePrepared(RenderPass* renderPass, RenderTarget* renderTarget,
                                     bool recordDrawStats) {
  if (preparedProgramInfo == nullptr || preparedProgram == nullptr ||
      preparedRenderTarget == nullptr) {
    LOGE("StandardDrawOp::executePrepared() DrawOp is not prepared!");
    return;
  }
  renderPass->setPipeline(preparedProgram->getPipeline());
  preparedProgramInfo->setUniformsAndSamplers(renderPass, preparedProgram.get());
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
    bool completeAOTDraw =
        preparedProgram->getProvenance().program == ProgramOrigin::PrecompiledArtifact;
    cache->recordDraw(drawDelta, completeAOTDraw);
  }
}
}  // namespace tgfx
