/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "Rect3DDrawOp.h"
#include "core/utils/MathExtra.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/Transform3DGeometryProcessor.h"
#include "inspect/InspectorMark.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {

// The maximum number of vertices per non-AA quad.
static constexpr uint32_t IndicesPerNonAAQuad = 6;
// The maximum number of vertices per AA quad.
static constexpr uint32_t IndicesPerAAQuad = 30;

PlacementPtr<Rect3DDrawOp> Rect3DDrawOp::Make(Context* context,
                                              PlacementPtr<RectsVertexProvider> provider,
                                              uint32_t renderFlags,
                                              const Rect3DDrawArgs& drawArgs) {
  if (provider == nullptr) {
    return nullptr;
  }
  auto drawOp = context->drawingBuffer()->make<Rect3DDrawOp>(provider.get(), drawArgs);
  CAPUTRE_RECT_MESH(drawOp.get(), provider.get());
  if (provider->aaType() == AAType::Coverage || provider->rectCount() > 1 ||
      provider->hasStroke()) {
    auto lineJoin = provider->hasStroke() ? std::make_optional(provider->lineJoin()) : std::nullopt;
    drawOp->indexBufferProxy = context->globalCache()->getRectIndexBuffer(
        provider->aaType() == AAType::Coverage, lineJoin);
  }
  if (provider->rectCount() <= 1) {
    // If we only have one rect, it is not worth the async task overhead.
    renderFlags |= RenderFlags::DisableAsyncTask;
  }
  drawOp->vertexBufferProxyView =
      context->proxyProvider()->createVertexBufferProxy(std::move(provider), renderFlags);
  return drawOp;
}

Rect3DDrawOp::Rect3DDrawOp(RectsVertexProvider* provider, const Rect3DDrawArgs& drawArgs)
    : DrawOp(provider->aaType()), drawArgs(drawArgs), rectCount(provider->rectCount()) {
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

PlacementPtr<GeometryProcessor> Rect3DDrawOp::onMakeGeometryProcessor(RenderTarget* renderTarget) {
  ATTRIBUTE_NAME("rectCount", static_cast<int>(rectCount));
  ATTRIBUTE_NAME("commonColor", commonColor);
  ATTRIBUTE_NAME("uvMatrix", uvMatrix);
  ATTRIBUTE_NAME("hasSubset", hasSubset);
  auto drawingBuffer = renderTarget->getContext()->drawingBuffer();
  // The actual size of the rendered texture is larger than the valid size, while the current
  // NDC coordinates were calculated based on the valid size, so they need to be adjusted
  // accordingly.
  //
  // NDC_Point is the projected vertex coordinate in NDC space, and NDC_Point_shifted is the
  // adjusted NDC coordinate. scale1 and offset1 are transformation parameters passed externally,
  // while scale2 and offset2 map the NDC coordinates from the valid space to the actual space.
  //
  // NDC_Point_shifted = ((NDC_Point * scale1) + offset1) * scale2 + offset2
  auto renderTargetW = static_cast<float>(renderTarget->width());
  DEBUG_ASSERT(!FloatNearlyZero(renderTargetW));
  auto renderTargetH = static_cast<float>(renderTarget->height());
  DEBUG_ASSERT(!FloatNearlyZero(renderTargetH));
  const Vec2 scale2(drawArgs.viewportSize.width / renderTargetW,
                    drawArgs.viewportSize.height / renderTargetH);
  Vec2 ndcScale = drawArgs.ndcScale * scale2;
  Vec2 ndcOffset = drawArgs.ndcOffset * scale2 + scale2 - Vec2(1.f, 1.f);
  if (renderTarget->origin() == ImageOrigin::BottomLeft) {
    ndcScale.y = -ndcScale.y;
    ndcOffset.y = -ndcOffset.y;
  }
  return Transform3DGeometryProcessor::Make(drawingBuffer, aaType, drawArgs.transformMatrix,
                                            ndcScale, ndcOffset);
}

void Rect3DDrawOp::onDraw(RenderPass* renderPass) {
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
    auto numIndicesPerQuad = aaType == AAType::Coverage ? IndicesPerAAQuad : IndicesPerNonAAQuad;
    renderPass->drawIndexed(PrimitiveType::Triangles, 0, rectCount * numIndicesPerQuad);
  } else {
    renderPass->draw(PrimitiveType::TriangleStrip, 0, 4);
  }
}

}  // namespace tgfx
