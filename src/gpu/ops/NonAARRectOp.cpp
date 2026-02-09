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

#include "NonAARRectOp.h"
#include "core/utils/ColorHelper.h"
#include "gpu/NonAARRectsVertexProvider.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/NonAARRectGeometryProcessor.h"
#include "inspect/InspectorMark.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
PlacementPtr<NonAARRectOp> NonAARRectOp::Make(Context* context,
                                              PlacementPtr<NonAARRectsVertexProvider> provider,
                                              uint32_t renderFlags) {
  if (provider == nullptr) {
    return nullptr;
  }
  auto allocator = context->drawingAllocator();
  auto drawOp = allocator->make<NonAARRectOp>(allocator, provider.get());
  drawOp->indexBufferProxy = context->globalCache()->getNonAARRectIndexBuffer();
  if (provider->rectCount() <= 1) {
    // If we only have one rect, it is not worth the async task overhead.
    renderFlags |= RenderFlags::DisableAsyncTask;
  }
  drawOp->vertexBufferProxyView =
      context->proxyProvider()->createVertexBufferProxy(std::move(provider), renderFlags);
  return drawOp;
}

NonAARRectOp::NonAARRectOp(BlockAllocator* allocator, NonAARRectsVertexProvider* provider)
    : DrawOp(allocator, AAType::None), rectCount(provider->rectCount()),
      hasStroke(provider->hasStroke()) {
  if (!provider->hasColor()) {
    commonColor = ToPMColor(provider->firstColor(), provider->dstColorSpace());
  }
}

PlacementPtr<GeometryProcessor> NonAARRectOp::onMakeGeometryProcessor(RenderTarget* renderTarget) {
  ATTRIBUTE_NAME("rectCount", static_cast<uint32_t>(rectCount));
  ATTRIBUTE_NAME("hasStroke", hasStroke);
  ATTRIBUTE_NAME("commonColor", commonColor);
  return NonAARRectGeometryProcessor::Make(allocator, renderTarget->width(),
                                           renderTarget->height(), hasStroke, commonColor);
}

void NonAARRectOp::onDraw(RenderPass* renderPass) {
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
