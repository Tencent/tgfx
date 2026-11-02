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
#include "core/utils/ColorHelper.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/Quad.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/RoundStrokeRectGeometryProcessor.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
PlacementPtr<RectDrawOp> RectDrawOp::Make(Context* context,
                                          PlacementPtr<RectsVertexProvider> provider,
                                          uint32_t renderFlags) {
  if (provider == nullptr) {
    return nullptr;
  }
  auto allocator = context->drawingAllocator();
  auto drawOp = allocator->make<RectDrawOp>(allocator, provider.get());
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

RectDrawOp::RectDrawOp(BlockAllocator* allocator, RectsVertexProvider* provider)
    : StandardDrawOp(allocator, provider->aaType()), rectCount(provider->rectCount()),
      lineJoin(provider->lineJoin()) {
  if (!provider->hasUVCoord()) {
    auto matrix = provider->firstMatrix();
    matrix.invert(&matrix);
    uvMatrix = matrix;
  }
  if (!provider->hasColor()) {
    commonColor = ToPMColor(provider->firstColor(), provider->dstColorSpace());
  }
  hasSubset = provider->hasSubset();
  // Fill rects interpolate their local coordinates affinely over the device quad: through the
  // shared inverse view matrix without UV coordinates, and through a single rect's rect-to-UV
  // mapping with them. Strokes and batches of several UV-mapped rects have no single mapping, and
  // neither has a scaled UV rect under coverage AA: its edge padding is applied to the UV rect in
  // rect units, which bends the mapping away from rect-to-UV.
  if (!provider->lineJoin().has_value()) {
    if (uvMatrix.has_value()) {
      deviceToLocal = uvMatrix;
    } else if (provider->rectCount() == 1) {
      Matrix inverse = {};
      const auto& rect = provider->firstRect();
      auto uvRect = provider->firstUVRect() != nullptr ? *provider->firstUVRect() : rect;
      bool paddedUV = provider->aaType() == AAType::Coverage &&
                      (uvRect.width() != rect.width() || uvRect.height() != rect.height());
      if (!rect.isEmpty() && !paddedUV && provider->firstMatrix().invert(&inverse)) {
        auto rectToUV = Matrix::MakeTrans(-rect.left, -rect.top);
        rectToUV.postScale(uvRect.width() / rect.width(), uvRect.height() / rect.height());
        rectToUV.postTranslate(uvRect.left, uvRect.top);
        rectToUV.preConcat(inverse);
        deviceToLocal = rectToUV;
      }
    }
  }
}

PlacementPtr<GeometryProcessor> RectDrawOp::onMakeGeometryProcessor(RenderTarget* renderTarget) {
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

void RectDrawOp::onDraw(RenderPass* renderPass, RenderTarget* /*renderTarget*/) {
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
  renderPass->setVertexBuffer(0, vertexBuffer->gpuBuffer(), vertexBufferProxyView->offset());
  renderPass->setIndexBuffer(indexBuffer ? indexBuffer->gpuBuffer() : nullptr);
  if (indexBuffer != nullptr) {
    const auto numIndicesPerQuad = GetNumIndicesPerQuad(aaType, lineJoin);
    renderPass->drawIndexed(PrimitiveType::Triangles,
                            static_cast<uint32_t>(rectCount * numIndicesPerQuad));
  } else {
    renderPass->draw(PrimitiveType::TriangleStrip, 4);
  }
}
}  // namespace tgfx
