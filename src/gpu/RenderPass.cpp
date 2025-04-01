/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "RenderPass.h"
#include "core/utils/Profiling.h"
#include "gpu/Gpu.h"

namespace tgfx {
bool RenderPass::begin(std::shared_ptr<RenderTarget> renderTarget,
                       std::shared_ptr<Texture> renderTexture) {
  if (renderTarget == nullptr) {
    return false;
  }
  _renderTarget = std::move(renderTarget);
  _renderTargetTexture = std::move(renderTexture);
  drawPipelineStatus = DrawPipelineStatus::NotConfigured;
  onBindRenderTarget();
  return true;
}

void RenderPass::end() {
  onUnbindRenderTarget();
  _renderTarget = nullptr;
  _renderTargetTexture = nullptr;
  _program = nullptr;
}

void RenderPass::bindProgramAndScissorClip(const ProgramInfo* programInfo,
                                           const Rect& scissorRect) {
  if (!onBindProgramAndScissorClip(programInfo, scissorRect)) {
    drawPipelineStatus = DrawPipelineStatus::FailedToBind;
    return;
  }
  drawPipelineStatus = DrawPipelineStatus::Ok;
}

void RenderPass::bindBuffers(std::shared_ptr<GpuBuffer> indexBuffer,
                             std::shared_ptr<GpuBuffer> vertexBuffer, size_t vertexOffset) {
  if (drawPipelineStatus != DrawPipelineStatus::Ok) {
    return;
  }
  if (!onBindBuffers(std::move(indexBuffer), std::move(vertexBuffer), vertexOffset, nullptr)) {
    drawPipelineStatus = DrawPipelineStatus::FailedToBind;
  }
}

void RenderPass::bindBuffers(std::shared_ptr<GpuBuffer> indexBuffer,
                             std::shared_ptr<Data> vertexData) {
  if (drawPipelineStatus != DrawPipelineStatus::Ok) {
    return;
  }
  if (!onBindBuffers(std::move(indexBuffer), nullptr, 0, std::move(vertexData))) {
    drawPipelineStatus = DrawPipelineStatus::FailedToBind;
  }
}

void RenderPass::draw(PrimitiveType primitiveType, size_t baseVertex, size_t vertexCount) {
  TRACE_DRAW(primitiveType == PrimitiveType::TriangleStrip ? vertexCount - 2 : vertexCount / 3);
  if (drawPipelineStatus != DrawPipelineStatus::Ok) {
    return;
  }
  onDraw(primitiveType, baseVertex, vertexCount, false);
  drawPipelineStatus = DrawPipelineStatus::NotConfigured;
}

void RenderPass::drawIndexed(PrimitiveType primitiveType, size_t baseIndex, size_t indexCount) {
  TRACE_DRAW(indexCount / 3);
  if (drawPipelineStatus != DrawPipelineStatus::Ok) {
    return;
  }
  onDraw(primitiveType, baseIndex, indexCount, true);
  drawPipelineStatus = DrawPipelineStatus::NotConfigured;
}

void RenderPass::clear(const Rect& scissor, Color color) {
  drawPipelineStatus = DrawPipelineStatus::NotConfigured;
  onClear(scissor, color);
}

void RenderPass::resolve(const Rect& bounds) {
  auto gpu = context->gpu();
  gpu->resolveRenderTarget(_renderTarget.get(), bounds);
  // Reset the render target after the resolve operation.
  onBindRenderTarget();
  drawPipelineStatus = DrawPipelineStatus::NotConfigured;
}

void RenderPass::copyToTexture(Texture* texture, int srcX, int srcY) {
  onCopyToTexture(texture, srcX, srcY);
  drawPipelineStatus = DrawPipelineStatus::NotConfigured;
}
}  // namespace tgfx
