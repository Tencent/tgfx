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
#include "gpu/processors/RoundStrokeRectGeometryProcessor.h"
#include "inspect/InspectorMark.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
PlacementPtr<RectDrawOp> RectDrawOp::Make(Context* context,
                                          PlacementPtr<RectsVertexProvider> provider,
                                          uint32_t renderFlags) {
  if (provider == nullptr) {
    return nullptr;
  }
  auto drawOp = context->drawingAllocator()->make<RectDrawOp>(provider.get());
  CAPUTRE_RECT_MESH(drawOp.get(), provider.get());
  if (provider->aaType() == AAType::Coverage || provider->rectCount() > 1 || provider->lineJoin()) {
    drawOp->indexBufferProxy = context->globalCache()->getRectIndexBuffer(
        provider->aaType() == AAType::Coverage, provider->lineJoin());
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
    : DrawOp(provider->aaType()), rectCount(provider->rectCount()), lineJoin(provider->lineJoin()) {
  if (!provider->hasUVCoord()) {
    auto matrix = provider->firstMatrix();
    matrix.invert(&matrix);
    uvMatrix = matrix;
  }
  if (!provider->hasColor()) {
    commonColor = provider->firstColor();
  }
  hasSubset = provider->hasSubset();
}

PlacementPtr<GeometryProcessor> RectDrawOp::onMakeGeometryProcessor(RenderTarget* renderTarget) {
  ATTRIBUTE_NAME("rectCount", static_cast<int>(rectCount));
  ATTRIBUTE_NAME("commonColor", commonColor);
  ATTRIBUTE_NAME("uvMatrix", uvMatrix);
  ATTRIBUTE_NAME("hasSubset", hasSubset);
  ATTRIBUTE_NAME("hasStroke", lineJoin.has_value());
  auto allocator = renderTarget->getContext()->drawingAllocator();
  if (lineJoin == LineJoin::Round) {
    return RoundStrokeRectGeometryProcessor::Make(allocator, aaType, commonColor, uvMatrix);
  }
  return QuadPerEdgeAAGeometryProcessor::Make(allocator, renderTarget->width(),
                                              renderTarget->height(), aaType, commonColor, uvMatrix,
                                              hasSubset);
}

static uint16_t GetNumIndicesPerQuad(AAType aaType, const std::optional<LineJoin>& lineJoin) {
  const auto isAA = aaType == AAType::Coverage;
  if (!lineJoin) {
    return isAA ? RectDrawOp::IndicesPerAAQuad : RectDrawOp::IndicesPerNonAAQuad;
  }

  switch (*lineJoin) {
    case LineJoin::Miter:
      return isAA ? RectDrawOp::IndicesPerAAMiterStrokeRect
                  : RectDrawOp::IndicesPerNonAAMiterStrokeRect;
    case LineJoin::Round:
      return isAA ? RectDrawOp::IndicesPerAARoundStrokeRect
                  : RectDrawOp::IndicesPerNonAARoundStrokeRect;
    case LineJoin::Bevel:
      return isAA ? RectDrawOp::IndicesPerAABevelStrokeRect
                  : RectDrawOp::IndicesPerNonAABevelStrokeRect;
    default:
      return 0;
  }
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
    const auto numIndicesPerQuad = GetNumIndicesPerQuad(aaType, lineJoin);
    renderPass->drawIndexed(PrimitiveType::Triangles, 0, rectCount * numIndicesPerQuad);
  } else {
    renderPass->draw(PrimitiveType::TriangleStrip, 0, 4);
  }
}
}  // namespace tgfx
