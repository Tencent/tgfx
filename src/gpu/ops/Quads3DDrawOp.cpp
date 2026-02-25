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

#include "Quads3DDrawOp.h"
#include "core/utils/ColorHelper.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "inspect/InspectorMark.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {

// The maximum number of indices per non-AA quad.
static constexpr uint32_t IndicesPerNonAAQuad = 6;
// The maximum number of indices per AA quad.
static constexpr uint32_t IndicesPerAAQuad = 30;

PlacementPtr<Quads3DDrawOp> Quads3DDrawOp::Make(Context* context,
                                                PlacementPtr<QuadsVertexProvider> provider,
                                                uint32_t renderFlags) {
  if (provider == nullptr) {
    return nullptr;
  }
  auto allocator = context->drawingAllocator();
  auto drawOp = allocator->make<Quads3DDrawOp>(allocator, provider.get());
  if (provider->aaType() == AAType::Coverage || provider->quadCount() > 1) {
    drawOp->indexBufferProxy = context->globalCache()->getRectIndexBuffer(
        provider->aaType() == AAType::Coverage, std::nullopt);
  }
  if (provider->quadCount() <= 1) {
    // If we only have one quad, it is not worth the async task overhead.
    renderFlags |= RenderFlags::DisableAsyncTask;
  }
  drawOp->vertexBufferProxyView =
      context->proxyProvider()->createVertexBufferProxy(std::move(provider), renderFlags);
  return drawOp;
}

Quads3DDrawOp::Quads3DDrawOp(BlockAllocator* allocator, QuadsVertexProvider* provider)
    : DrawOp(allocator, provider->aaType()), quadCount(provider->quadCount()) {
  if (!provider->hasUVCoord()) {
    auto matrix = provider->firstMatrix();
    matrix.invert(&matrix);
    uvMatrix = matrix;
  }
  if (!provider->hasColor()) {
    commonColor = ToPMColor(provider->firstColor(), nullptr);
  }
}

PlacementPtr<GeometryProcessor> Quads3DDrawOp::onMakeGeometryProcessor(RenderTarget* renderTarget) {
  ATTRIBUTE_NAME("quadCount", static_cast<int>(quadCount));
  ATTRIBUTE_NAME("commonColor", commonColor);
  ATTRIBUTE_NAME("uvMatrix", uvMatrix);
  return QuadPerEdgeAAGeometryProcessor::Make(allocator, renderTarget->width(),
                                              renderTarget->height(), aaType, commonColor, uvMatrix,
                                              false);
}

void Quads3DDrawOp::onDraw(RenderPass* renderPass) {
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
    auto numIndicesPerQuad = aaType == AAType::Coverage ? IndicesPerAAQuad : IndicesPerNonAAQuad;
    renderPass->drawIndexed(PrimitiveType::Triangles,
                            static_cast<uint32_t>(quadCount * numIndicesPerQuad));
  } else {
    renderPass->draw(PrimitiveType::TriangleStrip, 4);
  }
}

}  // namespace tgfx
