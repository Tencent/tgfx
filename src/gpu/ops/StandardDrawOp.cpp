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
#include "gpu/DrawingManager.h"
#include "gpu/GlobalCache.h"
#include "gpu/PermutationMatch.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/Program.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/AOTPointwiseChainProcessor.h"
#include "gpu/shaders/kernels/PerlinNoiseFillShader.h"
#include "gpu/shaders/kernels/PointwiseChainShader.h"

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
  preparedViaDecomposition = interpreterServesDraw(renderTarget);
  preparedProgram = preparedProgramInfo->getProgram(mode);
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

// Whether the shader the plain lookup resolved to is the chain interpreter — the AOT route's own
// direct serving. The perlin kernel serving a plain noise tree is the dedicated route, not the
// AOT entry's own.
static bool IsDirectAOTShader(const std::string& shaderName) {
  return shaderName == PointwiseChainShader::Name;
}

// The AOT entry's per-draw probe: reports whether the route serves this draw through the plain
// program lookup, for the direct-chain accounting. The re-expressing kernels serve unmodified
// draws by re-expressing the draw's own tree, so the probe only has to run the matcher and read
// which family won.
bool StandardDrawOp::interpreterServesDraw(RenderTarget* renderTarget) {
  auto cache = renderTarget->getContext()->precompiledShaderCache();
  if (!cache->isLoaded()) {
    return false;
  }
  if (!cache->decompositionEnabled()) {
    // Only the interpreter's extended kind serves gradients, so the required gradient lowering
    // keeps probing with the switch off; every other draw goes straight to the plain lookup.
    bool requiresGradientLowering = false;
    for (size_t index = 0; index < preparedProgramInfo->numFragmentProcessors(); ++index) {
      if (ContainsGradientSource(preparedProgramInfo->getFragmentProcessor(index))) {
        requiresGradientLowering = true;
        break;
      }
    }
    if (!requiresGradientLowering) {
      return false;
    }
  }
  auto& globalCache = *renderTarget->getContext()->globalCache();
  auto cached = globalCache.findProgram(preparedProgramInfo->programKey());
  if (cached != nullptr && cached->getProvenance().program == ProgramOrigin::PrecompiledArtifact) {
    // The program cache's key-to-program premise: this exact key already resolved to a
    // precompiled artifact, so the match that produced it succeeded and the plain route serves
    // the draw — the probe below would only repeat that walk.
    DEBUG_ASSERT(MatchPermutation(preparedProgramInfo.get()).has_value());
    return IsDirectAOTShader(cached->shaderName());
  }
  auto probe = MatchPermutation(preparedProgramInfo.get());
  return probe.has_value() && IsDirectAOTShader(probe->shaderName);
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
