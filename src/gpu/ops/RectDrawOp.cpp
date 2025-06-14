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

#include "RectDrawOp.h"
#include "gpu/ProxyProvider.h"
#include "gpu/Quad.h"
#include "gpu/ResourceProvider.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
PlacementPtr<RectDrawOp> RectDrawOp::Make(Context* context,
                                          PlacementPtr<RectsVertexProvider> provider,
                                          uint32_t renderFlags) {
  if (provider == nullptr) {
    return nullptr;
  }
  auto drawOp = context->drawingBuffer()->make<RectDrawOp>(provider.get());
  if (provider->aaType() == AAType::Coverage) {
    if (provider->hasStroke()) {
      drawOp->indexBufferProxy = context->resourceProvider()->aaStrokeRectIndexBuffer(LineJoin::Miter);
    }
    drawOp->indexBufferProxy = context->resourceProvider()->aaQuadIndexBuffer();
  } else if (provider->rectCount() > 1) {
    drawOp->indexBufferProxy = context->resourceProvider()->nonAAQuadIndexBuffer();
  }
  if (provider->rectCount() <= 1) {
    // If we only have one rect, it is not worth the async task overhead.
    renderFlags |= RenderFlags::DisableAsyncTask;
  }
  auto sharedVertexBuffer =
      context->proxyProvider()->createSharedVertexBuffer(std::move(provider), renderFlags);
  drawOp->vertexBufferProxy = sharedVertexBuffer.first;
  drawOp->vertexBufferOffset = sharedVertexBuffer.second;
  return drawOp;
}

RectDrawOp::RectDrawOp(RectsVertexProvider* provider)
    : DrawOp(provider->aaType()), rectCount(provider->rectCount()) {
  if (!provider->hasUVCoord()) {
    auto matrix = provider->firstMatrix();
    matrix.invert(&matrix);
    uvMatrix = matrix;
  }
  if (!provider->hasColor()) {
    commonColor = provider->firstColor();
  }
}

void RectDrawOp::execute(RenderPass* renderPass) {
  std::shared_ptr<GpuBuffer> indexBuffer;
  if (indexBufferProxy) {
    indexBuffer = indexBufferProxy->getBuffer();
    if (indexBuffer == nullptr) {
      return;
    }
  }
  std::shared_ptr<GpuBuffer> vertexBuffer =
      vertexBufferProxy ? vertexBufferProxy->getBuffer() : nullptr;
  if (vertexBuffer == nullptr) {
    return;
  }
  auto renderTarget = renderPass->renderTarget();
  auto drawingBuffer = renderPass->getContext()->drawingBuffer();
  auto gp = QuadPerEdgeAAGeometryProcessor::Make(
      drawingBuffer, renderTarget->width(), renderTarget->height(), aaType, commonColor, uvMatrix);
//  Matrix matrix = Matrix::I();
//  matrix.postTranslate(79.5, 79.5);
//  auto gp = DefaultGeometryProcessor::Make(
//      drawingBuffer,  Color::Red(),renderTarget->width(), renderTarget->height(), aaType, matrix, matrix);
  auto pipeline = createPipeline(renderPass, std::move(gp));
  renderPass->bindProgramAndScissorClip(pipeline.get(), scissorRect());
  renderPass->bindBuffers(indexBuffer, vertexBuffer, vertexBufferOffset);
  if (indexBuffer != nullptr) {
    uint16_t numIndicesPerQuad;
    if (aaType == AAType::Coverage) {
//      numIndicesPerQuad = ResourceProvider::NumIndicesPerAAQuad();
        numIndicesPerQuad = ResourceProvider::NumIndicesStrokeRect(LineJoin::Miter);
    } else {
      numIndicesPerQuad = ResourceProvider::NumIndicesPerNonAAQuad();
    }
    renderPass->drawIndexed(PrimitiveType::Triangles, 0, rectCount * numIndicesPerQuad);
  } else {
    renderPass->draw(PrimitiveType::TriangleStrip, 0, 4);
  }
}
}  // namespace tgfx
