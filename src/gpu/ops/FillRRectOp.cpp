/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "FillRRectOp.h"
#include "core/utils/ColorHelper.h"
#include "gpu/FillRRectsVertexProvider.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/FillRRectGeometryProcessor.h"
#include "inspect/InspectorMark.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
PlacementPtr<FillRRectOp> FillRRectOp::Make(Context* context,
                                            PlacementPtr<FillRRectsVertexProvider> provider,
                                            uint32_t renderFlags) {
  if (provider == nullptr) {
    return nullptr;
  }
  auto allocator = context->drawingAllocator();
  auto drawOp = allocator->make<FillRRectOp>(allocator, provider.get());
  drawOp->indexBufferProxy = context->globalCache()->getFillRRectIndexBuffer();
  if (provider->rectCount() <= 1) {
    // If we only have one rect, it is not worth the async task overhead.
    renderFlags |= RenderFlags::DisableAsyncTask;
  }
  drawOp->vertexBufferProxyView =
      context->proxyProvider()->createVertexBufferProxy(std::move(provider), renderFlags);
  return drawOp;
}

FillRRectOp::FillRRectOp(BlockAllocator* allocator, FillRRectsVertexProvider* provider)
    : DrawOp(allocator, provider->aaType()), rectCount(provider->rectCount()) {
  if (!provider->hasColor()) {
    commonColor = ToPMColor(provider->firstColor(), provider->dstColorSpace());
  }
}

PlacementPtr<GeometryProcessor> FillRRectOp::onMakeGeometryProcessor(RenderTarget* renderTarget) {
  ATTRIBUTE_NAME("rectCount", static_cast<uint32_t>(rectCount));
  ATTRIBUTE_NAME("commonColor", commonColor);
  return FillRRectGeometryProcessor::Make(allocator, renderTarget->width(), renderTarget->height(),
                                          aaType, commonColor);
}

void FillRRectOp::onDraw(RenderPass* renderPass) {
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
  renderPass->drawIndexed(PrimitiveType::Triangles,
                          static_cast<uint32_t>(rectCount * IndicesPerRRect));
}
}  // namespace tgfx
