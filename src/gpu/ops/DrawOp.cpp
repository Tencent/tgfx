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
#include "core/utils/Log.h"
#include "gpu/Program.h"

namespace tgfx {
void DrawOp::execute(RenderPass* renderPass, RenderTarget* renderTarget) {
  if (usesStandardPipeline()) {
    if (!bindStandardPipeline(renderPass, renderTarget)) {
      return;
    }
  }
  onDraw(renderPass, renderTarget);
}

void DrawOp::applyScissor(RenderPass* renderPass, RenderTarget* renderTarget) const {
  if (scissorRect.isEmpty()) {
    renderPass->setScissorRect(0, 0, renderTarget->width(), renderTarget->height());
    return;
  }
  // Clamp scissor rect to render target bounds. Without this clamping, an out-of-bounds
  // scissor produced by clipping (e.g. when the clip extends past the device) can trip
  // backend validation errors or be silently rejected by the driver.
  int scissorX = std::max(0, static_cast<int>(scissorRect.x()));
  int scissorY = std::max(0, static_cast<int>(scissorRect.y()));
  int scissorRight =
      std::min(renderTarget->width(), static_cast<int>(scissorRect.x() + scissorRect.width()));
  int scissorBottom =
      std::min(renderTarget->height(), static_cast<int>(scissorRect.y() + scissorRect.height()));
  int scissorWidth = std::max(0, scissorRight - scissorX);
  int scissorHeight = std::max(0, scissorBottom - scissorY);
  renderPass->setScissorRect(scissorX, scissorY, scissorWidth, scissorHeight);
}

bool DrawOp::bindStandardPipeline(RenderPass* renderPass, RenderTarget* renderTarget) {
  auto geometryProcessor = onMakeGeometryProcessor(renderTarget);
  if (geometryProcessor == nullptr) {
    return false;
  }
  std::vector<FragmentProcessor*> fragmentProcessors = {};
  fragmentProcessors.reserve(colors.size() + coverages.size());
  for (auto& color : colors) {
    fragmentProcessors.emplace_back(color.get());
  }
  for (auto& coverage : coverages) {
    fragmentProcessors.emplace_back(coverage.get());
  }
  ProgramInfo programInfo(renderTarget, geometryProcessor.get(), std::move(fragmentProcessors),
                          colors.size(), xferProcessor.get(), blendMode);
  programInfo.setCullMode(cullMode);
  onConfigureProgramInfo(programInfo);
  auto program = programInfo.getProgram();
  if (program == nullptr) {
    LOGE("DrawOp::bindStandardPipeline() Failed to get the program!");
    return false;
  }
  renderPass->setPipeline(program->getPipeline());
  programInfo.setUniformsAndSamplers(renderPass, program.get());
  applyScissor(renderPass, renderTarget);
  return true;
}
}  // namespace tgfx
