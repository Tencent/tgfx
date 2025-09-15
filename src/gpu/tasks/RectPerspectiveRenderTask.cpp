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

#include "RectPerspectiveRenderTask.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RectsVertexProvider.h"
#include "gpu/ops/RectDrawOp.h"
#include "gpu/processors/QuadPerEdgeAA3DGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

RectPerspectiveRenderTask::RectPerspectiveRenderTask(
    std::shared_ptr<RenderTargetProxy> renderTarget, const Rect& rect, AAType aa,
    std::shared_ptr<TextureProxy> fillTexture, const Matrix3D& transformMatrix)
    : renderTarget(std::move(renderTarget)), rect(rect), aa(aa),
      fillTexture(std::move(fillTexture)), transformMatrix(transformMatrix) {
  const auto drawingBuffer = this->renderTarget->getContext()->drawingBuffer();
  if (drawingBuffer == nullptr) {
    LOGE("RectPerspectiveRenderTask::execute() Drawing buffer is null!");
    return;
  }

  auto vertexProvider = RectsVertexProvider::MakeFrom(drawingBuffer, rect, aa);
  vertexBufferProxyView =
      this->renderTarget->getContext()->proxyProvider()->createVertexBufferProxy(
          std::move(vertexProvider));
  if (aa == AAType::Coverage) {
    indexBufferProxy = this->renderTarget->getContext()->globalCache()->getRectIndexBuffer(true);
  }
}

void RectPerspectiveRenderTask::execute(CommandEncoder* encoder) {
  if (vertexBufferProxyView == nullptr || fillTexture == nullptr) {
    LOGE("RectPerspectiveRenderTask::execute() Vertext buffer proxy view or fill texture is null!");
    return;
  }
  const auto rt = renderTarget->getRenderTarget();
  if (rt == nullptr) {
    LOGE("RectPerspectiveRenderTask::execute() Render target is null!");
    return;
  }
  const auto drawingBuffer = renderTarget->getContext()->drawingBuffer();
  if (drawingBuffer == nullptr) {
    LOGE("RectPerspectiveRenderTask::execute() Drawing buffer is null!");
    return;
  }

  constexpr auto loadOp = LoadAction::Clear;
  const RenderPassDescriptor descriptor(rt->getRenderTexture(), loadOp, StoreAction::Store,
                                        Color::Transparent(), nullptr);
  const auto renderPass = encoder->beginRenderPass(descriptor);
  if (renderPass == nullptr) {
    LOGE("RectPerspectiveRenderTask::execute() Failed to initialize the render pass!");
    return;
  }

  const Vec2 ndcScale(rect.width() / static_cast<float>(rt->width()),
                      rect.height() / static_cast<float>(rt->height()));
  const auto ndcRect = transformMatrix.mapRect(rect);
  const auto ndcRectScaled =
      Rect::MakeXYWH(ndcRect.left * ndcScale.x, ndcRect.top * ndcScale.y,
                     ndcRect.width() * ndcScale.x, ndcRect.height() * ndcScale.y);
  const Vec2 ndcOffset(-1.f - ndcRectScaled.left, -1.f - ndcRectScaled.top);
  const auto geometryProcessor = QuadPerEdgeAA3DGeometryProcessor::Make(
      drawingBuffer, aa, transformMatrix, ndcScale, ndcOffset);
  const SamplingArgs samplingArgs = {TileMode::Decal, TileMode::Decal, {}, SrcRectConstraint::Fast};
  const auto uvMatrix = Matrix::MakeTrans(-rect.left, -rect.top);
  const auto fragmentProcessor = TextureEffect::Make(fillTexture, samplingArgs, &uvMatrix);
  const ProgramInfo programInfo((rt.get()), geometryProcessor.get(), {fragmentProcessor.get()}, 1,
                                nullptr, BlendMode::SrcOver);
  const auto program = std::static_pointer_cast<PipelineProgram>(programInfo.getProgram());
  if (program == nullptr) {
    LOGE("RectPerspectiveRenderTask::execute() Failed to get the program!");
    renderPass->end();
    return;
  }

  renderPass->setPipeline(program->getPipeline());
  programInfo.setUniformsAndSamplers(renderPass.get(), program.get());

  const auto vertexBuffer = vertexBufferProxyView->getBuffer();
  const auto indexBuffer = indexBufferProxy ? indexBufferProxy->getBuffer() : nullptr;
  renderPass->setVertexBuffer(vertexBuffer->gpuBuffer(), vertexBufferProxyView->offset());
  renderPass->setIndexBuffer(indexBuffer ? indexBuffer->gpuBuffer() : nullptr);

  if (indexBuffer != nullptr) {
    const auto numIndicesPerQuad =
        (aa == AAType::Coverage ? IndicesPerAAQuad : IndicesPerNonAAQuad);
    renderPass->drawIndexed(PrimitiveType::Triangles, 0, numIndicesPerQuad);
  } else {
    renderPass->draw(PrimitiveType::TriangleStrip, 0, 4);
  }
  renderPass->end();
}

}  // namespace tgfx
