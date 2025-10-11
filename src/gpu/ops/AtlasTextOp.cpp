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

#include "AtlasTextOp.h"
#include "RectDrawOp.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "inspect/InspectorMark.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {

PlacementPtr<AtlasTextOp> AtlasTextOp::Make(Context* context,
                                            PlacementPtr<RectsVertexProvider> provider,
                                            uint32_t renderFlags,
                                            std::shared_ptr<TextureProxy> textureProxy,
                                            const SamplingOptions& sampling) {
  if (provider == nullptr || textureProxy == nullptr || textureProxy->width() <= 0 ||
      textureProxy->height() <= 0) {
    return nullptr;
  }
  auto atlasTextOp = context->drawingBuffer()->make<AtlasTextOp>(provider.get(),
                                                                 std::move(textureProxy), sampling);
  CAPUTRE_RECT_MESH(atlasTextOp.get(), provider.get());
  if (provider->aaType() == AAType::Coverage || provider->rectCount() > 1) {
    atlasTextOp->indexBufferProxy =
        context->globalCache()->getRectIndexBuffer(provider->aaType() == AAType::Coverage);
  }
  if (provider->rectCount() <= 1) {
    // If we only have one rect, it is not worth the async task overhead.
    renderFlags |= RenderFlags::DisableAsyncTask;
  }
  atlasTextOp->vertexBufferProxyView =
      context->proxyProvider()->createVertexBufferProxy(std::move(provider), renderFlags);
  return atlasTextOp;
}

AtlasTextOp::AtlasTextOp(RectsVertexProvider* provider, std::shared_ptr<TextureProxy> textureProxy,
                         const SamplingOptions& sampling)
    : DrawOp(provider->aaType()), rectCount(provider->rectCount()),
      textureProxy(std::move(textureProxy)), sampling(sampling) {
  if (!provider->hasColor()) {
    commonColor = provider->firstColor();
  }
}

PlacementPtr<GeometryProcessor> AtlasTextOp::onMakeGeometryProcessor(RenderTarget* renderTarget) {
  ATTRIBUTE_NAME("rectCount", static_cast<uint32_t>(rectCount));
  ATTRIBUTE_NAME("commonColor", commonColor);
  auto drawingBuffer = renderTarget->getContext()->drawingBuffer();
  return AtlasTextGeometryProcessor::Make(drawingBuffer, textureProxy, aaType, commonColor,
                                          sampling);
}

void AtlasTextOp::onDraw(RenderPass* renderPass) {
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
    uint16_t numIndicesPerQuad;
    if (aaType == AAType::Coverage) {
      numIndicesPerQuad = RectDrawOp::IndicesPerAAQuad;
    } else {
      numIndicesPerQuad = RectDrawOp::IndicesPerNonAAQuad;
    }
    renderPass->drawIndexed(PrimitiveType::Triangles, 0, rectCount * numIndicesPerQuad);
  } else {
    renderPass->draw(PrimitiveType::TriangleStrip, 0, 4);
  }
}

bool AtlasTextOp::hasCoverage() const {
  return true;
}
}  // namespace tgfx
