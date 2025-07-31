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
#include "gpu/GPUBuffer.h"
#include "core/DataSource.h"
#include "core/utils/Profiling.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
PlacementPtr<RRectDrawOp> RRectDrawOp::Make(Context* context,
                                            PlacementPtr<RRectsVertexProvider> provider,
                                            uint32_t renderFlags) {
  if (provider == nullptr) {
    return nullptr;
  }
  auto drawOp = context->drawingBuffer()->make<RRectDrawOp>(provider.get());
  drawOp->indexBufferProxy = context->globalCache()->getRRectIndexBuffer(provider->hasStroke());
  if (provider->rectCount() <= 1) {
    // If we only have one rect, it is not worth the async task overhead.
    renderFlags |= RenderFlags::DisableAsyncTask;
  }
  drawOp->vertexBufferProxy =
      context->proxyProvider()->createVertexBuffer(std::move(provider), renderFlags);
  return drawOp;
}

RRectDrawOp::RRectDrawOp(RRectsVertexProvider* provider)
    : DrawOp(provider->aaType()), rectCount(provider->rectCount()), useScale(provider->useScale()) {
  if (!provider->hasColor()) {
    commonColor = provider->firstColor();
  }
  hasStroke = provider->hasStroke();
}

void RRectDrawOp::execute(RenderPass* renderPass) {
  OperateMark(inspector::OpTaskType::RRectDrawOp);
  AttributeName("rectCount", static_cast<uint32_t>(rectCount));
  AttributeName("useScale", useScale);
  AttributeName("hasStroke", hasStroke);
  AttributeTGFXName("commonColor", commonColor);
  AttributeNameEnum("blenderMode", getBlendMode(), inspector::CustomEnumType::BlendMode);
  AttributeNameEnum("aaType", getAAType(), inspector::CustomEnumType::AAType);
  if (indexBufferProxy == nullptr || vertexBufferProxy == nullptr) {
    return;
  }
  auto indexBuffer = indexBufferProxy->getBuffer();
  if (indexBuffer == nullptr) {
    return;
  }
  std::shared_ptr<GPUBuffer> vertexBuffer = vertexBufferProxy->getBuffer();
  if (vertexBuffer == nullptr) {
    return;
  }
  auto renderTarget = renderPass->getRenderTarget();
  auto drawingBuffer = renderPass->getContext()->drawingBuffer();
  auto gp =
      EllipseGeometryProcessor::Make(drawingBuffer, renderTarget->width(), renderTarget->height(),
                                     hasStroke, useScale, commonColor);
  auto pipeline = createPipeline(renderPass, std::move(gp));
  renderPass->bindProgramAndScissorClip(pipeline.get(), scissorRect());
  renderPass->bindBuffers(indexBuffer, vertexBuffer, vertexBufferProxy->offset());
  auto numIndicesPerRRect = hasStroke ? IndicesPerStrokeRRect : IndicesPerFillRRect;
  renderPass->drawIndexed(PrimitiveType::Triangles, 0, rectCount * numIndicesPerRRect);
}
}  // namespace tgfx
