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

#include "RRectDrawOp.h"
#include "core/DataSource.h"
#include "core/utils/ColorHelper.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "inspect/InspectorMark.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
PlacementPtr<RRectDrawOp> RRectDrawOp::Make(Context* context,
                                            PlacementPtr<RRectsVertexProvider> provider,
                                            uint32_t renderFlags) {
  if (provider == nullptr) {
    return nullptr;
  }
  auto allocator = context->drawingAllocator();
  auto drawOp = allocator->make<RRectDrawOp>(allocator, provider.get());
  CAPUTRE_RRECT_MESH(drawOp.get(), provider.get());
  drawOp->indexBufferProxy = context->globalCache()->getRRectIndexBuffer(provider->hasStroke());
  if (provider->rectCount() <= 1) {
    // If we only have one rect, it is not worth the async task overhead.
    renderFlags |= RenderFlags::DisableAsyncTask;
  }
  drawOp->vertexBufferProxyView =
      context->proxyProvider()->createVertexBufferProxy(std::move(provider), renderFlags);
  return drawOp;
}

RRectDrawOp::RRectDrawOp(BlockAllocator* allocator, RRectsVertexProvider* provider)
    : DrawOp(allocator, provider->aaType()), rectCount(provider->rectCount()) {
  if (!provider->hasColor()) {
    commonColor = ToPMColor(provider->firstColor(), provider->dstColorSpace());
  }
  hasStroke = provider->hasStroke();
}

PlacementPtr<GeometryProcessor> RRectDrawOp::onMakeGeometryProcessor(RenderTarget* renderTarget) {
  ATTRIBUTE_NAME("rectCount", static_cast<uint32_t>(rectCount));
  ATTRIBUTE_NAME("hasStroke", hasStroke);
  ATTRIBUTE_NAME("commonColor", commonColor);
  return EllipseGeometryProcessor::Make(allocator, renderTarget->width(), renderTarget->height(),
                                        hasStroke, commonColor);
}

void RRectDrawOp::onDraw(RenderPass* renderPass) {
  if (indexBufferProxy == nullptr || vertexBufferProxyView == nullptr) {
    return;
  }
  auto indexBuffer = indexBufferProxy->getBuffer();
  if (indexBuffer == nullptr) {
    return;
  }
  auto vertexBuffer = vertexBufferProxyView->getBuffer();
  if (vertexBuffer == nullptr) {
    return;
  }
  renderPass->setVertexBuffer(0, vertexBuffer->gpuBuffer(), vertexBufferProxyView->offset());
  renderPass->setIndexBuffer(indexBuffer->gpuBuffer());
  auto numIndicesPerRRect = hasStroke ? IndicesPerStrokeRRect : IndicesPerFillRRect;
  renderPass->drawIndexed(PrimitiveType::Triangles, rectCount * numIndicesPerRRect);
}
}  // namespace tgfx
