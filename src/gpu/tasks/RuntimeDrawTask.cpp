/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "RuntimeDrawTask.h"
#include "gpu/GlobalCache.h"
#include "gpu/Pipeline.h"
#include "gpu/ProxyProvider.h"
#include "gpu/Quad.h"
#include "gpu/RectsVertexProvider.h"
#include "gpu/RenderPass.h"
#include "gpu/RuntimeProgramCreator.h"
#include "gpu/RuntimeProgramWrapper.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
RuntimeDrawTask::RuntimeDrawTask(std::shared_ptr<RenderTargetProxy> target,
                                 std::vector<std::shared_ptr<TextureProxy>> inputs,
                                 std::shared_ptr<RuntimeEffect> effect, const Point& offset)
    : renderTargetProxy(std::move(target)), inputTextures(std::move(inputs)),
      effect(std::move(effect)), offset(offset) {
  auto context = renderTargetProxy->getContext();
  inputVertexBuffers.reserve(inputTextures.size());
  for (auto& input : inputTextures) {
    if (input != nullptr) {
      auto maskRect = Rect::MakeWH(input->width(), input->height());
      auto maskVertexProvider =
          RectsVertexProvider::MakeFrom(context->drawingBuffer(), maskRect, AAType::None);
      auto maskBuffer = context->proxyProvider()->createVertexBufferProxy(
          std::move(maskVertexProvider), RenderFlags::DisableAsyncTask);
      inputVertexBuffers.push_back(std::move(maskBuffer));
    } else {
      inputVertexBuffers.push_back(nullptr);
    }
  }
}

void RuntimeDrawTask::execute(CommandEncoder* encoder) {
  std::vector<std::shared_ptr<Texture>> textures = {};
  textures.reserve(inputTextures.size());
  for (size_t i = 0; i < inputTextures.size(); i++) {
    std::shared_ptr<Texture> texture;
    if (auto inputProxy = inputTextures[i]) {
      texture = GetFlatTexture(encoder, std::move(inputProxy), inputVertexBuffers[i].get());
    }
    if (texture == nullptr) {
      LOGE("RuntimeDrawTask::execute() Failed to get the input %d texture!", i);
      return;
    }
    textures.push_back(texture);
  }
  auto renderTarget = renderTargetProxy->getRenderTarget();
  if (renderTarget == nullptr) {
    LOGE("RuntimeDrawTask::execute() Failed to get the render target!");
    return;
  }
  auto context = renderTarget->getContext();
  RuntimeProgramCreator programCreator(effect);
  auto program = context->globalCache()->getProgram(&programCreator);
  if (program == nullptr) {
    LOGE("RuntimeDrawTask::execute() Failed to create the runtime program!");
    return;
  }
  std::vector<BackendTexture> backendTextures = {};
  backendTextures.reserve(textures.size());
  for (auto& texture : textures) {
    backendTextures.push_back(texture->getBackendTexture());
  }
  effect->onDraw(RuntimeProgramWrapper::Unwrap(program.get()), backendTextures,
                 renderTarget->getBackendRenderTarget(), offset);
  if (renderTarget->sampleCount() > 1) {
    auto renderPass = encoder->beginRenderPass(renderTarget, true);
    DEBUG_ASSERT(renderPass != nullptr);
    renderPass->end();
  }
}

std::shared_ptr<Texture> RuntimeDrawTask::GetFlatTexture(
    CommandEncoder* encoder, std::shared_ptr<TextureProxy> textureProxy,
    VertexBufferProxyView* vertexBufferProxyView) {
  auto texture = textureProxy->getTexture();
  if (texture == nullptr) {
    return nullptr;
  }
  if (!texture->isYUV() && texture->getSampler()->type() == SamplerType::TwoD &&
      texture->origin() == ImageOrigin::TopLeft) {
    return texture;
  }
  auto vertexBuffer = vertexBufferProxyView ? vertexBufferProxyView->getBuffer() : nullptr;
  if (vertexBuffer == nullptr) {
    return nullptr;
  }
  auto context = texture->getContext();
  auto renderTargetProxy = RenderTargetProxy::MakeFallback(
      context, texture->width(), texture->height(), texture->isAlphaOnly(), 1,
      texture->hasMipmaps(), ImageOrigin::TopLeft, BackingFit::Exact);
  if (renderTargetProxy == nullptr) {
    LOGE("RuntimeDrawTask::getFlatTexture() Failed to create the render target!");
    return nullptr;
  }
  auto renderTarget = renderTargetProxy->getRenderTarget();
  auto renderPass = encoder->beginRenderPass(renderTarget, false);
  if (renderPass == nullptr) {
    LOGE("RuntimeDrawTask::getFlatTexture() Failed to initialize the render pass!");
    return nullptr;
  }
  auto colorProcessor = TextureEffect::Make(std::move(textureProxy));
  if (colorProcessor == nullptr) {
    LOGE("RuntimeDrawTask::getFlatTexture() Failed to create the color processor!");
    return nullptr;
  }
  auto geometryProcessor =
      DefaultGeometryProcessor::Make(context->drawingBuffer(), {}, renderTarget->width(),
                                     renderTarget->height(), AAType::None, {}, {});
  auto format = renderTarget->format();
  auto caps = renderPass->getContext()->caps();
  const auto& swizzle = caps->getWriteSwizzle(format);
  std::vector<PlacementPtr<FragmentProcessor>> fragmentProcessors = {};
  fragmentProcessors.emplace_back(std::move(colorProcessor));
  auto pipeline =
      std::make_unique<Pipeline>(std::move(geometryProcessor), std::move(fragmentProcessors), 1,
                                 nullptr, BlendMode::Src, &swizzle);
  renderPass->bindProgramAndScissorClip(pipeline.get(), {});
  renderPass->bindBuffers(nullptr, vertexBuffer->gpuBuffer(), vertexBufferProxyView->offset());
  renderPass->draw(PrimitiveType::TriangleStrip, 0, 4);
  renderPass->end();
  return renderTarget->asTexture();
}

}  // namespace tgfx
