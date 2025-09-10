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

void RectPerspectiveRenderTask::execute(CommandEncoder* encoder) {
  if (fillTexture == nullptr) {
    LOGE("RectPerspectiveRenderTask::execute() Fill texture is null!");
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

  constexpr auto loadOp = LoadAction::Load;
  const RenderPassDescriptor descriptor(rt->getRenderTexture(), loadOp, StoreAction::Store,
                                        Color::Transparent(), nullptr);
  const auto renderPass = encoder->beginRenderPass(descriptor);
  if (renderPass == nullptr) {
    LOGE("RectPerspectiveRenderTask::execute() Failed to initialize the render pass!");
    return;
  }

  const auto geometryProcessor = QuadPerEdgeAA3DGeometryProcessor::Make(drawingBuffer, aa);
  const SamplingArgs samplingArgs = {TileMode::Decal, TileMode::Decal, {}, SrcRectConstraint::Fast};
  const auto fragmentProcessor = TextureEffect::Make(fillTexture, samplingArgs);
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

  auto vertexProvider = RectsVertexProvider::MakeFrom(drawingBuffer, rect, aa);
  const auto vertexBufferProxyView =
      renderTarget->getContext()->proxyProvider()->createVertexBufferProxy(
          std::move(vertexProvider));
  const auto vertexBuffer = vertexBufferProxyView->getBuffer();
  std::shared_ptr<IndexBuffer> indexBuffer = nullptr;
  if (aa == AAType::Coverage) {
    const auto indexBufferProxy =
        renderTarget->getContext()->globalCache()->getRectIndexBuffer(true);
    indexBuffer = indexBufferProxy->getBuffer();
  }
  renderPass->setVertexBuffer(vertexBuffer->gpuBuffer(), 0);
  renderPass->setIndexBuffer(indexBuffer->gpuBuffer());

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
