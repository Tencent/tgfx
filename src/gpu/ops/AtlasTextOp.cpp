/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "gpu/ProxyProvider.h"
#include "gpu/Quad.h"
#include "gpu/RenderPass.h"
#include "gpu/ResourceProvider.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
PlacementPtr<AtlasTextOp> AtlasTextOp::Make(Context* context,
                                            PlacementPtr<RectsVertexProvider> provider,
                                            uint32_t renderFlags, int atlasWidth, int atlasHeight) {
  if (provider == nullptr || atlasWidth == 0 || atlasHeight == 0) {
    return nullptr;
  }
  auto atlasTextOp =
      context->drawingBuffer()->make<AtlasTextOp>(provider.get(), atlasWidth, atlasHeight);
  if (provider->aaType() == AAType::Coverage) {
    atlasTextOp->indexBufferProxy = context->resourceProvider()->aaQuadIndexBuffer();
  } else if (provider->rectCount() > 1) {
    atlasTextOp->indexBufferProxy = context->resourceProvider()->nonAAQuadIndexBuffer();
  }
  if (provider->rectCount() <= 1) {
    // If we only have one rect, it is not worth the async task overhead.
    renderFlags |= RenderFlags::DisableAsyncTask;
  }
  auto sharedVertexBuffer =
      context->proxyProvider()->createSharedVertexBuffer(std::move(provider), renderFlags);
  atlasTextOp->vertexBufferProxy = sharedVertexBuffer.first;
  atlasTextOp->vertexBufferOffset = sharedVertexBuffer.second;
  return atlasTextOp;
}

AtlasTextOp::AtlasTextOp(RectsVertexProvider* provider, int atlasWidth, int atlasHeight)
    : DrawOp(provider->aaType()), rectCount(provider->rectCount()), atlasWidth(atlasWidth),
      atlasHeight(atlasHeight) {
  if (!provider->hasColor()) {
    commonColor = provider->firstColor();
  }
}

void AtlasTextOp::execute(RenderPass* renderPass) {
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

  auto drawingBuffer = renderPass->getContext()->drawingBuffer();
  auto gp =
      AtlasTextGeometryProcessor::Make(drawingBuffer, atlasWidth, atlasHeight, aaType, commonColor);
  auto pipeline = createPipeline(renderPass, std::move(gp));
  renderPass->bindProgramAndScissorClip(pipeline.get(), scissorRect());
  renderPass->bindBuffers(indexBuffer, vertexBuffer, vertexBufferOffset);
  if (indexBuffer != nullptr) {
    uint16_t numIndicesPerQuad;
    if (aaType == AAType::Coverage) {
      numIndicesPerQuad = ResourceProvider::NumIndicesPerAAQuad();
    } else {
      numIndicesPerQuad = ResourceProvider::NumIndicesPerNonAAQuad();
    }
    renderPass->drawIndexed(PrimitiveType::Triangles, 0, rectCount * numIndicesPerQuad);
  } else {
    renderPass->draw(PrimitiveType::TriangleStrip, 0, 4);
  }
}
}  // namespace tgfx
