/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "TextureFlattenTask.h"
#include "core/utils/Log.h"
#include "gpu/Gpu.h"
#include "gpu/Pipeline.h"
#include "gpu/Quad.h"
#include "gpu/RenderPass.h"
#include "gpu/Texture.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {
TextureFlattenTask::TextureFlattenTask(UniqueKey uniqueKey,
                                       std::shared_ptr<TextureProxy> textureProxy)
    : uniqueKey(std::move(uniqueKey)), sourceTextureProxy(std::move(textureProxy)) {
}

bool TextureFlattenTask::prepare(Context* context) {
  auto texture = sourceTextureProxy->getTexture();
  if (texture == nullptr) {
    return false;
  }
  if (!texture->isYUV() && texture->getSampler()->type() == SamplerType::TwoD &&
      texture->origin() == ImageOrigin::TopLeft) {
    return false;
  }
  auto alphaRenderable = context->caps()->isFormatRenderable(PixelFormat::ALPHA_8);
  auto format =
      texture->isAlphaOnly() && alphaRenderable ? PixelFormat::ALPHA_8 : PixelFormat::RGBA_8888;
  flatTexture = Texture::MakeFormat(context, texture->width(), texture->height(), format,
                                    texture->hasMipmaps(), ImageOrigin::TopLeft);
  if (flatTexture == nullptr) {
    LOGE("TextureFlattenTask::prepare() Failed to create the flat texture!");
  }
  renderTarget = RenderTarget::MakeFrom(flatTexture.get());
  if (renderTarget == nullptr) {
    LOGE("TextureFlattenTask::prepare() Failed to create the render target!");
  }
  return true;
}

bool TextureFlattenTask::execute(RenderPass* renderPass) {
  if (renderTarget == nullptr) {
    return false;
  }
  if (!renderPass->begin(renderTarget, flatTexture)) {
    LOGE("TextureFlattenTask::execute() Failed to initialize the render pass!");
    return false;
  }
  auto colorProcessor = TextureEffect::Make(sourceTextureProxy);
  if (colorProcessor == nullptr) {
    LOGE("TextureFlattenTask::execute() Failed to create the color processor!");
    return false;
  }
  auto drawingBuffer = renderPass->getContext()->drawingBuffer();
  auto geometryProcessor = DefaultGeometryProcessor::Make(
      drawingBuffer, {}, renderTarget->width(), renderTarget->height(), AAType::None, {}, {});
  auto format = renderPass->renderTarget()->format();
  auto caps = renderPass->getContext()->caps();
  const auto& swizzle = caps->getWriteSwizzle(format);
  std::vector<PlacementPtr<FragmentProcessor>> fragmentProcessors = {};
  fragmentProcessors.emplace_back(std::move(colorProcessor));
  auto pipeline =
      std::make_unique<Pipeline>(std::move(geometryProcessor), std::move(fragmentProcessors), 1,
                                 nullptr, BlendMode::Src, &swizzle, false);
  auto quad = Quad::MakeFrom(Rect::MakeWH(renderTarget->width(), renderTarget->height()));
  auto vertexData = quad.toTriangleStrips();
  renderPass->bindProgramAndScissorClip(pipeline.get(), {});
  renderPass->bindBuffers(nullptr, vertexData);
  renderPass->draw(PrimitiveType::TriangleStrip, 0, 4);
  renderPass->end();
  // Assign the unique key to the flat texture after the render pass is done.
  flatTexture->assignUniqueKey(uniqueKey);
  return true;
}

}  // namespace tgfx
