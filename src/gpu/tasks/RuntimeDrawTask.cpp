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
#include "core/utils/ColorSpaceHelper.h"
#include "gpu/GlobalCache.h"
#include "gpu/Program.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ProxyProvider.h"
#include "gpu/Quad.h"
#include "gpu/RectsVertexProvider.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "inspect/InspectorMark.h"
#include "tgfx/core/RenderFlags.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {
RuntimeDrawTask::RuntimeDrawTask(std::shared_ptr<RenderTargetProxy> target,
                                 std::vector<RuntimeInputTexture> inputs,
                                 std::shared_ptr<RuntimeEffect> effect, const Point& offset)
    : renderTargetProxy(std::move(target)), inputTextures(std::move(inputs)),
      effect(std::move(effect)), offset(offset) {
  auto context = renderTargetProxy->getContext();
  inputVertexBuffers.reserve(inputTextures.size());
  for (auto& input : inputTextures) {
    auto textureProxy = input.textureProxy;
    if (textureProxy != nullptr) {
      auto maskRect = Rect::MakeWH(textureProxy->width(), textureProxy->height());
      auto maskVertexProvider =
          RectsVertexProvider::MakeFrom(context->drawingAllocator(), maskRect, AAType::None);
      auto maskBuffer = context->proxyProvider()->createVertexBufferProxy(
          std::move(maskVertexProvider), RenderFlags::DisableAsyncTask);
      inputVertexBuffers.push_back(std::move(maskBuffer));
    } else {
      inputVertexBuffers.push_back(nullptr);
    }
  }
}

void RuntimeDrawTask::execute(CommandEncoder* encoder) {
  TASK_MARK(tgfx::inspect::OpTaskType::RuntimeDrawTask);
  std::vector<std::shared_ptr<TextureView>> flatTextures = {};
  flatTextures.reserve(inputTextures.size());
  for (size_t i = 0; i < inputTextures.size(); i++) {
    std::shared_ptr<TextureView> textureView = nullptr;
    textureView = GetFlatTextureView(encoder, &inputTextures[i], inputVertexBuffers[i].get(),
                                     inputTextures[0].colorSpace);
    if (textureView == nullptr) {
      LOGE("RuntimeDrawTask::execute() Failed to get the input %d texture view!", i);
      return;
    }
    flatTextures.push_back(textureView);
  }
  auto renderTarget = renderTargetProxy->getRenderTarget();
  if (renderTarget == nullptr) {
    LOGE("RuntimeDrawTask::execute() Failed to get the render target!");
    return;
  }
  std::vector<std::shared_ptr<Texture>> inputs = {};
  inputs.reserve(flatTextures.size());
  for (auto& textureView : flatTextures) {
    inputs.push_back(textureView->getTexture());
  }
  if (!effect->onDraw(encoder, inputs, renderTarget->getRenderTexture(), offset)) {
    LOGE("RuntimeDrawTask::execute() Failed to draw the runtime effect!");
  }
}

std::shared_ptr<TextureView> RuntimeDrawTask::GetFlatTextureView(
    CommandEncoder* encoder, const RuntimeInputTexture* textureProxyWithCS,
    VertexBufferView* vertexBufferProxyView, std::shared_ptr<ColorSpace> dstColorSpace) {
  auto textureProxy = textureProxyWithCS->textureProxy;
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto textureView = textureProxy->getTextureView();
  if (textureView == nullptr) {
    return nullptr;
  }
  if (!textureView->isYUV() && textureView->getTexture()->type() == TextureType::TwoD &&
      textureView->origin() == ImageOrigin::TopLeft &&
      !NeedConvertColorSpace(textureProxyWithCS->colorSpace, dstColorSpace)) {
    return textureView;
  }
  auto vertexBuffer = vertexBufferProxyView ? vertexBufferProxyView->getBuffer() : nullptr;
  if (vertexBuffer == nullptr) {
    return nullptr;
  }
  auto context = textureView->getContext();
  auto renderTargetProxy = RenderTargetProxy::Make(
      context, textureView->width(), textureView->height(), textureView->isAlphaOnly(), 1,
      textureView->hasMipmaps(), ImageOrigin::TopLeft, BackingFit::Exact);
  if (renderTargetProxy == nullptr) {
    LOGE("RuntimeDrawTask::getFlatTexture() Failed to create the render target!");
    return nullptr;
  }
  auto renderTarget = renderTargetProxy->getRenderTarget();
  RenderPassDescriptor descriptor(renderTarget->getRenderTexture());
  auto renderPass = encoder->beginRenderPass(descriptor);
  if (renderPass == nullptr) {
    LOGE("RuntimeDrawTask::getFlatTexture() Failed to initialize the render pass!");
    return nullptr;
  }
  auto allocator = context->drawingAllocator();
  auto colorProcessor = TextureEffect::Make(allocator, std::move(textureProxy), {}, nullptr, false);
  if (colorProcessor == nullptr) {
    LOGE("RuntimeDrawTask::getFlatTexture() Failed to create the color processor!");
    return nullptr;
  }
  if (!textureView->isAlphaOnly() &&
      NeedConvertColorSpace(textureProxyWithCS->colorSpace, dstColorSpace)) {
    auto xformEffect = ColorSpaceXformEffect::Make(
        context->drawingAllocator(), textureProxyWithCS->colorSpace.get(), AlphaType::Premultiplied,
        dstColorSpace.get(), AlphaType::Premultiplied);
    colorProcessor = FragmentProcessor::Compose(context->drawingAllocator(), std::move(xformEffect),
                                                std::move(colorProcessor));
  }
  auto geometryProcessor =
      DefaultGeometryProcessor::Make(context->drawingAllocator(), {}, renderTarget->width(),
                                     renderTarget->height(), AAType::None, {}, {});
  std::vector fragmentProcessors = {colorProcessor.get()};
  ProgramInfo programInfo(renderTarget.get(), geometryProcessor.get(),
                          std::move(fragmentProcessors), 1, nullptr, BlendMode::Src);
  auto program = programInfo.getProgram();
  if (program == nullptr) {
    LOGE("RuntimeDrawTask::GetFlatTextureView() Failed to get the program!");
    return nullptr;
  }
  renderPass->setPipeline(program->getPipeline());
  programInfo.setUniformsAndSamplers(renderPass.get(), program.get());
  renderPass->setVertexBuffer(vertexBuffer->gpuBuffer(), vertexBufferProxyView->offset());
  renderPass->draw(PrimitiveType::TriangleStrip, 0, 4);
  renderPass->end();
  return renderTarget->asTextureView();
}

}  // namespace tgfx
