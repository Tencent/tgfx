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
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/Program.h"
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

bool StandardDrawOp::prepare(RenderTarget* renderTarget,
                             std::optional<ColorProcessorList> colorOverride,
                             PixelFormat depthStencilFormat, bool isFinalLookup) {
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
  preparedViaDecomposition = false;
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
  // The draw's record-time IR rides along whenever the info's processor list is the unmodified
  // record-time list: the re-expressing families then reuse it instead of lowering a second time.
  // A color override (a plan task's rebuilt pass colors) describes a different expression.
  if (!preparedColors.has_value() && loweredIR() != nullptr) {
    preparedProgramInfo->setCarriedIR(loweredIR());
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
                                    bool isFinalLookup) {
  // Mirror execute()'s ordering: onPrepare() may veto the op (empty asynchronous buffers);
  // skipping it here would leave those ops drawing from null resources ().
  if (!onPrepare()) {
    return false;
  }
  return prepare(renderTarget, std::move(colorOverride), PixelFormat::Unknown, isFinalLookup);
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
