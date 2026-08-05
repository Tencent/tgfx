/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "DrawOp.h"
#include <algorithm>
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/Program.h"

namespace tgfx {
bool DrawOp::prepare(RenderTarget* renderTarget, ProgramLookupMode mode,
                     std::optional<ColorProcessorList> colorOverride) {
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
  preparedProgram = preparedProgramInfo->getProgram(mode);
  if (preparedProgram == nullptr) {
    LOGE("DrawOp::prepare() Failed to get the program!");
    return false;
  }
  preparedRenderTarget = renderTarget;
  return true;
}

void DrawOp::executePrepared(RenderPass* renderPass, bool recordDrawStats) {
  if (preparedProgramInfo == nullptr || preparedProgram == nullptr ||
      preparedRenderTarget == nullptr) {
    LOGE("DrawOp::executePrepared() DrawOp is not prepared!");
    return;
  }
  renderPass->setPipeline(preparedProgram->getPipeline());
  preparedProgramInfo->setUniformsAndSamplers(renderPass, preparedProgram.get());
  if (offscreenFillSource.has_value()) {
    auto cache = preparedRenderTarget->getContext()->precompiledShaderCache();
    cache->recordOffscreenFillProgram(*offscreenFillSource, offscreenFillCanExecute,
                                      preparedProgram->getProvenance().program);
  }

  if (scissorRect.isEmpty()) {
    renderPass->setScissorRect(0, 0, preparedRenderTarget->width(), preparedRenderTarget->height());
  } else {
    // Clamp scissor rect to render target bounds
    int scissorX = std::max(0, static_cast<int>(scissorRect.x()));
    int scissorY = std::max(0, static_cast<int>(scissorRect.y()));
    int scissorRight = std::min(preparedRenderTarget->width(),
                                static_cast<int>(scissorRect.x() + scissorRect.width()));
    int scissorBottom = std::min(preparedRenderTarget->height(),
                                 static_cast<int>(scissorRect.y() + scissorRect.height()));
    int scissorWidth = std::max(0, scissorRight - scissorX);
    int scissorHeight = std::max(0, scissorBottom - scissorY);
    renderPass->setScissorRect(scissorX, scissorY, scissorWidth, scissorHeight);
  }
  onDraw(renderPass);

  auto cache = preparedRenderTarget->getContext()->precompiledShaderCache();
  if (recordDrawStats && cache->diagnosticRecordingEnabled()) {
    AOTDrawStats drawDelta = {};
    drawDelta.kernelInvocations = 1;
    bool completeAOTDraw =
        preparedProgram->getProvenance().program == ProgramOrigin::PrecompiledArtifact;
    cache->recordDraw(drawDelta, completeAOTDraw);
  }
}

void DrawOp::execute(RenderPass* renderPass, RenderTarget* renderTarget) {
  if (prepare(renderTarget)) {
    executePrepared(renderPass);
  }
}
}  // namespace tgfx
