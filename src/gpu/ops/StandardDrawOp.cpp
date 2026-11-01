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
#include "gpu/DrawingManager.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/Program.h"
#include "gpu/aot/AOTChainBuilder.h"
#include "gpu/aot/AOTProbeCounters.h"
#include "gpu/aot/AOTProgramInfo.h"
#include "gpu/aot/DrawIR.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/Processor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/AOTShapeRules.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"

#include "gpu/shaders/kernels/PointwiseChainShader.h"

namespace tgfx {

void StandardDrawOp::execute(RenderPass* renderPass, RenderTarget* renderTarget) {
  if (!onPrepare()) {
    return;
  }
  if (!prepare(renderTarget, std::nullopt, renderPass->depthStencilFormat())) {
    return;
  }
  executePrepared(renderPass, renderTarget);
}

// Whether the shader the plain lookup resolved to is the chain interpreter — the AOT route's own
// direct serving. The perlin kernel serving a plain noise tree is the dedicated route, not the
// AOT entry's own.
static bool IsDirectAOTShader(const std::string& shaderName) {
  return shaderName == PointwiseChainShader::Name;
}

#if DEBUG
namespace {
// The texture-batch safety net: for the single-leaf color shape the texture rows read, the IR
// leaf's record-time facts must agree with the tree's one texture processor. A disagreement is
// a record-versus-prepare drift (a pending upload's instantiation, a proxy/texture disagreement)
// that would make the executor-table rows read different facts than the processor walk did.
// Multi-leaf shapes (blend DAGs, a mask-modulated shader) have no unambiguous correspondence
// here and are skipped — the rows' shapes are single-leaf.
void AssertSingleLeafFacts(const DrawIR& ir,
                            const std::vector<PlacementPtr<FragmentProcessor>>& colors) {
  const AOTTextureParameters* leaf = nullptr;
  size_t leafCount = 0;
  for (uint32_t index = 0; index < ir.color.nodeCount(); ++index) {
    auto* node = ir.color.nodeAt(AOTNodeID(index));
    if (node != nullptr && node->kind == AOTEffectKind::TextureSource) {
      leaf = std::get_if<AOTTextureParameters>(&node->parameters);
      leafCount++;
    }
  }
  if (leafCount != 1 || leaf == nullptr) {
    return;
  }
  const FragmentProcessor* texture = nullptr;
  size_t textureCount = 0;
  for (const auto& color : colors) {
    FragmentProcessor::Iter iter(color.get());
    while (auto* fp = iter.next()) {
      if (IsProcessor<TiledTextureEffect>(fp) || IsProcessor<DeviceSpaceTextureEffect>(fp) ||
          IsProcessor<TextureEffect>(fp)) {
        texture = fp;
        textureCount++;
      }
    }
  }
  if (textureCount != 1 || texture == nullptr) {
    return;
  }
  auto classOf = [](const FragmentProcessor* fp) {
    if (IsProcessor<TiledTextureEffect>(fp)) {
      return AOTTextureSamplingKind::Tiled;
    }
    if (IsProcessor<DeviceSpaceTextureEffect>(fp)) {
      return AOTTextureSamplingKind::Device;
    }
    return AOTTextureSamplingKind::Plain;
  };
  // A failure here means the leaf's record-time facts drifted from the tree's processor.
  DEBUG_ASSERT(classOf(texture) == leaf->samplingKind);
  if (leaf->samplingKind == AOTTextureSamplingKind::Plain) {
    auto* te = static_cast<const TextureEffect*>(texture);
    DEBUG_ASSERT(te->isYUV() == leaf->isYUV && te->isAlphaOnly() == leaf->isAlphaOnly &&
                 te->hasRGBAAA() == leaf->hasRGBAAA && te->hasPerspective() == leaf->hasPerspective);
  } else if (leaf->samplingKind == AOTTextureSamplingKind::Tiled) {
    auto* tiled = static_cast<const TiledTextureEffect*>(texture);
    DEBUG_ASSERT(tiled->isAlphaOnly() == leaf->isAlphaOnly &&
                 tiled->hasPerspective() == leaf->hasPerspective);
  }
}
}  // namespace
#endif

bool StandardDrawOp::prepare(RenderTarget* renderTarget,
                             std::optional<ColorProcessorList> colorOverride,
                             PixelFormat depthStencilFormat, bool isFinalLookup,
                             const DrawIR* passIR) {
  if (!geometryProcessorInitialized) {
    geometryProcessorInitialized = true;
    geometryProcessor = onMakeGeometryProcessor(renderTarget);
  }
  if (geometryProcessor == nullptr) {
    return false;
  }
#if DEBUG
  // The record-time geometry descriptor must agree with the processor the op actually built:
  // an op that self-reports wrong would mis-route the executor-table matching silently. The
  // whole suite runs this on every IR-carrying draw, so the coverage is every drawable shape.
  if (ir != nullptr) {
    auto actual = DescribeGeometryProcessor(geometryProcessor.get());
    // A failure here means a recordTimeGeometry() override is wrong or missing (the record-time
    // geometry descriptor drifted from the made geometry processor).
    DEBUG_ASSERT(ir->geometry.kind == actual.kind && ir->geometry.hasCommonColor == actual.hasCommonColor &&
                 ir->geometry.hasUVMatrix == actual.hasUVMatrix &&
                 ir->geometry.hasSubset == actual.hasSubset &&
                 ir->geometry.hasUVPerspective == actual.hasUVPerspective);
    // The xfer twin: the record-time classification asks the processor's own dst info, the
    // match-time one the instantiated view — a disagreement means the dst-texture timing
    // assumption is wrong somewhere, and the executor-table matching would mis-route.
    DEBUG_ASSERT(ir->xfer == ClassifyXferForMatch(xferProcessor.get()));
    // The leaf twin runs only against the unmodified record-time color list — a color override
    // (a plan task's pass colors) describes a different expression than the record-time IR.
    if (!colorOverride.has_value()) {
      AssertSingleLeafFacts(*ir, colors);
    }
  }
#endif

  preparedProgram = nullptr;
  preparedProgramInfo = nullptr;
  preparedRenderTarget = nullptr;
  preparedViaDecomposition = false;
  preparedColors = std::move(colorOverride);
  auto& activeColors = preparedColors.has_value() ? *preparedColors : colors;

  std::vector<FragmentProcessor*> fragmentProcessors = {};
  size_t colorCount = activeColors.size();
  // Plan-pass serving: the pass IR is the whole expression, so the AOT route resolves the draw
  // from the program it compiles onto — no processor travels through the info. A fill pass carries
  // the IR on itself; the terminal pass hands it in from the plan.
  const DrawIR* resolvedPassIR = passIR;
  if (resolvedPassIR == nullptr) {
    auto* lowered = loweredIR();
    if (lowered != nullptr && lowered->passDescriptor.has_value()) {
      resolvedPassIR = lowered;
    }
  }
  if (resolvedPassIR != nullptr) {
    auto context = renderTarget->getContext();
    if (context == nullptr || context->drawingManager() == nullptr) {
      return false;
    }
    // The pass IR compiles onto the chain program, whose variant plus the pipeline state form the
    // key — everything resolves before any cache lookup. The pass info carries no processors: the
    // program is the whole expression, and the blend formula's coverage bit is false (the kernel
    // folds coverage itself).
    AOTProgram aotProgram = {};
    if (!AOTChainBuilder::CompilePassProgram(*resolvedPassIR, &aotProgram)) {
      return false;
    }
    auto passInfo = std::make_unique<ProgramInfo>(renderTarget, geometryProcessor.get(),
                                                  xferProcessor.get(), blendMode, false);
    passInfo->setCullMode(cullMode);
    if (depthStencilFormat != PixelFormat::Unknown) {
      DepthStencilDescriptor depthStencil = {};
      depthStencil.format = depthStencilFormat;
      passInfo->setDepthStencil(depthStencil);
    }
    onConfigureProgramInfo(*passInfo);
    AOTProgramInfo aotInfo(*passInfo, aotProgram);
    if (!aotInfo.valid()) {
      // The pass's shape has no variant, or the bundle lacks it: nothing can serve this draw.
      return false;
    }
    passInfo->setCachedChainProgram(std::move(aotProgram));
    preparedProgramInfo = std::move(passInfo);
    preparedProgram = aotInfo.program();
    preparedViaDecomposition = true;
    preparedRenderTarget = renderTarget;
    return true;
  } else {
    fragmentProcessors.reserve(activeColors.size() + coverages.size());
    for (auto& color : activeColors) {
      fragmentProcessors.emplace_back(color.get());
    }
    for (auto& coverage : coverages) {
      fragmentProcessors.emplace_back(coverage.get());
    }
  }
  preparedProgramInfo = std::make_unique<ProgramInfo>(renderTarget, geometryProcessor.get(),
                                                      std::move(fragmentProcessors), colorCount,
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
  // The draw's record-time IR rides along whenever the info's processor list is the unmodified
  // record-time list: the re-expressing families then reuse it instead of lowering a second time.
  // A color override (a plan task's rebuilt pass colors) describes a different expression, and so
  // does an expression that grew at prepare — a rasterized shape attaches its mask coverage FP
  // only once the shape proxy exists (onMakeGeometryProcessor), so the counts the IR was lowered
  // from must still match the op's live lists.
  if (!preparedColors.has_value() && loweredIR() != nullptr &&
      loweredIR()->colorFPCount == activeColors.size() &&
      loweredIR()->coverageFPCount == coverages.size()) {
    preparedProgramInfo->setCarriedIR(loweredIR());
    // P4.4 decision probe (diagnostic only): the noise draw's geometry form, classified where
    // the real geometry processor and the carried IR finally coexist (record-time GP is null).
    // Default is the form today's NoiseGraph variants do not cover.
    bool hasNoise = false;
    for (uint32_t i = 0; i < loweredIR()->color.nodeCount(); ++i) {
      auto* node = loweredIR()->color.nodeAt(AOTNodeID(i));
      hasNoise |= node != nullptr && node->kind == AOTEffectKind::PerlinNoiseSource;
    }
    if (hasNoise) {
      auto* probes = AOTProbe();
      if (IsProcessor<DefaultGeometryProcessor>(geometryProcessor.get())) {
        probes->noiseGPDefault.fetch_add(1, std::memory_order_relaxed);
      } else if (IsProcessor<QuadPerEdgeAAGeometryProcessor>(geometryProcessor.get())) {
        probes->noiseGPQuad.fetch_add(1, std::memory_order_relaxed);
      } else {
        probes->noiseGPOther.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }
  preparedProgram = preparedProgramInfo->getProgram();
  if (preparedProgram == nullptr) {
    if (isFinalLookup) {
      // The final lookup for this draw (plan-task pass probes fall back to the original draw):
      // nothing will be drawn, so the drop must be visible.
      LOGE("StandardDrawOp::prepare() No program for the draw; the draw is dropped!");
      auto cache = renderTarget->getContext()->precompiledShaderCache();
      if (cache->diagnosticRecordingEnabled()) {
        cache->recordDroppedDraw();
      }
    }
    return false;
  }
  // The direct-chain accounting derives from the resolved program: the plain lookup served this
  // draw through the chain interpreter when the program it resolved to is the chain shader.
  preparedViaDecomposition =
      preparedProgram->getProvenance().program == ProgramOrigin::PrecompiledArtifact &&
      IsDirectAOTShader(preparedProgram->shaderName());
  preparedRenderTarget = renderTarget;
  return true;
}

bool StandardDrawOp::prepareForTask(RenderTarget* renderTarget,
                                    std::optional<ColorProcessorList> colorOverride,
                                    bool isFinalLookup, const DrawIR* passIR) {
  // Mirror execute()'s ordering: onPrepare() may veto the op (empty asynchronous buffers);
  // skipping it here would leave those ops drawing from null resources ().
  if (!onPrepare()) {
    return false;
  }
  return prepare(renderTarget, std::move(colorOverride), PixelFormat::Unknown, isFinalLookup,
                 passIR);
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
