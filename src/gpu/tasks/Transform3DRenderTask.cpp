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

#include "Transform3DRenderTask.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RectsVertexProvider.h"
#include "gpu/ops/RectDrawOp.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/Transform3DGeometryProcessor.h"

namespace tgfx {

// The maximum number of vertices per non-AA quad.
static constexpr uint32_t IndicesPerNonAAQuad = 6;
// The maximum number of vertices per AA quad.
static constexpr uint32_t IndicesPerAAQuad = 30;

Transform3DRenderTask::Transform3DRenderTask(const Rect& rect,
                                             std::shared_ptr<RenderTargetProxy> renderTarget,
                                             std::shared_ptr<TextureProxy> fillTexture,
                                             const PerspectiveRenderArgs& args)
    : rect(rect), renderTarget(std::move(renderTarget)), fillTexture(std::move(fillTexture)),
      args(args) {
  const auto drawingBuffer = this->renderTarget->getContext()->drawingBuffer();
  if (drawingBuffer == nullptr) {
    LOGE("RectPerspectiveRenderTask::execute() Drawing buffer is null!");
    return;
  }

  auto vertexProvider = RectsVertexProvider::MakeFrom(drawingBuffer, rect, args.aa);
  vertexBufferProxyView =
      this->renderTarget->getContext()->proxyProvider()->createVertexBufferProxy(
          std::move(vertexProvider));
  if (args.aa == AAType::Coverage) {
    indexBufferProxy = this->renderTarget->getContext()->globalCache()->getRectIndexBuffer(true);
  }
}

void Transform3DRenderTask::execute(CommandEncoder* encoder) {
  if (vertexBufferProxyView == nullptr || fillTexture == nullptr) {
    LOGE("RectPerspectiveRenderTask::execute() Vertex buffer proxy view or fill texture is null!");
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

  // The actual size of the rendered texture is larger than the valid size, while the current
  // NDC coordinates were calculated based on the valid size, so they need to be adjusted
  // accordingly.
  //
  // NDC_Point is the projected vertex coordinate in NDC space, and NDC_Point_shifted is the
  // adjusted NDC coordinate. scale1 and offset1 are transformation parameters passed externally,
  // while scale2 and offset2 map the NDC coordinates from the valid space to the actual space.
  //
  // NDC_Point_shifted = ((NDC_Point * scale1) + offset1) * scale2 + offset2
  const Vec2 scale2(static_cast<float>(renderTarget->width()) / static_cast<float>(rt->width()),
                    static_cast<float>(renderTarget->height()) / static_cast<float>(rt->height()));
  Vec2 ndcScale = args.ndcScale * scale2;
  Vec2 ndcOffset = args.ndcOffset * scale2 + scale2 - Vec2(1.f, 1.f);
  if (renderTarget->origin() == ImageOrigin::BottomLeft) {
    ndcScale.y = -ndcScale.y;
    ndcOffset.y = -ndcOffset.y;
  }
  const auto geometryProcessor = Transform3DGeometryProcessor::Make(
      drawingBuffer, args.aa, args.transformMatrix, ndcScale, ndcOffset);
  const SamplingArgs samplingArgs = {TileMode::Decal, TileMode::Decal, {}, SrcRectConstraint::Fast};
  const auto uvMatrix = Matrix::MakeTrans(-rect.left, -rect.top);
  const auto fragmentProcessor = TextureEffect::Make(fillTexture, samplingArgs, &uvMatrix);
  const ProgramInfo programInfo((rt.get()), geometryProcessor.get(), {fragmentProcessor.get()}, 1,
                                nullptr, BlendMode::Src);
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
        (args.aa == AAType::Coverage ? IndicesPerAAQuad : IndicesPerNonAAQuad);
    renderPass->drawIndexed(PrimitiveType::Triangles, 0, numIndicesPerQuad);
  } else {
    renderPass->draw(PrimitiveType::TriangleStrip, 0, 4);
  }
  renderPass->end();
}

}  // namespace tgfx
