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

#include "RectDrawOp.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/Quad.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "inspect/InspectorMark.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
PlacementPtr<RectDrawOp> RectDrawOp::Make(Context* context,
                                          PlacementPtr<RectsVertexProvider> provider,
                                          uint32_t renderFlags) {
  if (provider == nullptr) {
    return nullptr;
  }
  auto drawOp = context->drawingBuffer()->make<RectDrawOp>(provider.get());
  CAPUTRE_RECT_MESH(drawOp.get(), provider.get());
  if (provider->aaType() == AAType::Coverage || provider->rectCount() > 1 ||
      provider->hasStroke()) {
    if (provider->hasStroke()) {
      drawOp->indexBufferProxy = context->globalCache()->getStrokeRectIndexBuffer(
          provider->aaType() == AAType::Coverage, provider->lineJoin());
    } else {
      drawOp->indexBufferProxy =
          context->globalCache()->getRectIndexBuffer(provider->aaType() == AAType::Coverage);
    }
  } else if (provider->rectCount() > 1) {
    drawOp->indexBufferProxy = context->globalCache()->getRectIndexBuffer(false);
  }
  if (provider->rectCount() <= 1) {
    // If we only have one rect, it is not worth the async task overhead.
    renderFlags |= RenderFlags::DisableAsyncTask;
  }
  drawOp->vertexBufferProxyView =
      context->proxyProvider()->createVertexBufferProxy(std::move(provider), renderFlags);
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
  hasSubset = provider->hasSubset();
  hasStroke = provider->hasStroke();
  strokeLineJoin = provider->lineJoin();
}

PlacementPtr<GeometryProcessor> RectDrawOp::onMakeGeometryProcessor(RenderTarget* renderTarget) {
  ATTRIBUTE_NAME("rectCount", static_cast<int>(rectCount));
  ATTRIBUTE_NAME("commonColor", commonColor);
  ATTRIBUTE_NAME("uvMatrix", uvMatrix);
  ATTRIBUTE_NAME("hasSubset", hasSubset);
  auto drawingBuffer = renderTarget->getContext()->drawingBuffer();
  return QuadPerEdgeAAGeometryProcessor::Make(drawingBuffer, renderTarget->width(),
                                              renderTarget->height(), aaType, commonColor, uvMatrix,
                                              hasSubset);
}

static uint16_t GetNumIndicesPerQuad(AAType aaType, bool hasStroke, LineJoin lineJoin) {
  if (!hasStroke) {
    return aaType == AAType::Coverage ? RectDrawOp::IndicesPerAAQuad
                                      : RectDrawOp::IndicesPerNonAAQuad;
  }

  if (aaType == AAType::Coverage) {
    return lineJoin == LineJoin::Miter ? RectDrawOp::IndicesPerAAMiterStrokeRect
                                       : RectDrawOp::IndicesPerAABevelStrokeRect;
  }

  return lineJoin == LineJoin::Miter ? RectDrawOp::IndicesPerNonAAMiterStrokeRect
                                     : RectDrawOp::IndicesPerNonAABevelStrokeRect;
}

void RectDrawOp::onDraw(RenderPass* renderPass) {
  std::shared_ptr<BufferResource> indexBuffer = nullptr;
  if (indexBufferProxy) {
    indexBuffer = indexBufferProxy->getBuffer();
    if (indexBuffer == nullptr) {
      return;
    }
  }
  auto vertexBuffer = vertexBufferProxyView ? vertexBufferProxyView->getBuffer() : nullptr;
  if (vertexBuffer == nullptr) {
    return;
  }
  renderPass->setVertexBuffer(vertexBuffer->gpuBuffer(), vertexBufferProxyView->offset());
  renderPass->setIndexBuffer(indexBuffer ? indexBuffer->gpuBuffer() : nullptr);
  if (indexBuffer != nullptr) {
    auto numIndicesPerQuad = GetNumIndicesPerQuad(aaType, hasStroke, strokeLineJoin);
    renderPass->drawIndexed(PrimitiveType::Triangles, 0, rectCount * numIndicesPerQuad);
  } else {
    renderPass->draw(PrimitiveType::TriangleStrip, 0, 4);
  }
}
}  // namespace tgfx
