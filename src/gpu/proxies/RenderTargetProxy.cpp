/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "RenderTargetProxy.h"
#include "RenderTargetWrapper.h"
#include "TextureRenderTargetProxy.h"
#include "gpu/Gpu.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
std::shared_ptr<RenderTargetProxy> RenderTargetProxy::MakeFrom(
    std::shared_ptr<RenderTarget> renderTarget) {
  if (renderTarget == nullptr && renderTarget->getContext() != nullptr) {
    return nullptr;
  }
  return std::shared_ptr<RenderTargetWrapper>(new RenderTargetWrapper(std::move(renderTarget)));
}

std::shared_ptr<RenderTargetProxy> RenderTargetProxy::MakeFrom(std::shared_ptr<Texture> texture,
                                                               int sampleCount) {
  if (texture == nullptr) {
    return nullptr;
  }
  auto context = texture->getContext();
  if (context == nullptr) {
    return nullptr;
  }
  auto format = texture->getSampler()->format;
  if (!context->caps()->isFormatRenderable(format)) {
    return nullptr;
  }
  auto textureProxy = context->proxyProvider()->wrapTexture(std::move(texture));
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<TextureRenderTargetProxy>(
      new TextureRenderTargetProxy(std::move(textureProxy), format, sampleCount, true));
}

std::shared_ptr<RenderTargetProxy> RenderTargetProxy::Make(Context* context, int width, int height,
                                                           PixelFormat format, int sampleCount,
                                                           bool mipMapped) {
  if (context == nullptr) {
    return nullptr;
  }
  auto caps = context->caps();
  if (!caps->isFormatRenderable(format)) {
    return nullptr;
  }
  sampleCount = caps->getSampleCount(sampleCount, format);
  auto textureProxy =
      context->proxyProvider()->createTextureProxy(width, height, format, mipMapped);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<TextureRenderTargetProxy>(
      new TextureRenderTargetProxy(std::move(textureProxy), format, sampleCount, false));
}

RenderTargetProxy::RenderTargetProxy(std::shared_ptr<RenderTarget> renderTarget)
    : renderTarget(std::move(renderTarget)) {
}

bool RenderTargetProxy::isTextureBacked() const {
  return getTextureProxy() != nullptr;
}

std::shared_ptr<Texture> RenderTargetProxy::getTexture() const {
  auto textureProxy = getTextureProxy();
  return textureProxy ? textureProxy->getTexture() : nullptr;
}

std::shared_ptr<RenderTarget> RenderTargetProxy::getRenderTarget() const {
  return renderTarget;
}

bool RenderTargetProxy::isInstantiated() const {
  return renderTarget != nullptr;
}

bool RenderTargetProxy::instantiate() {
  if (renderTarget != nullptr) {
    return true;
  }
  renderTarget = onMakeRenderTarget();
  return renderTarget != nullptr;
}

std::shared_ptr<TextureProxy> RenderTargetProxy::makeTextureProxy(bool copyContent) const {
  //TODO: send an async texture copying task to the gpu context.
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto context = getContext();
  auto textureProxy = getTextureProxy();
  auto hasMipmaps = textureProxy && textureProxy->hasMipmaps();
  auto newTextureProxy = context->proxyProvider()->createTextureProxy(width(), height(), format(),
                                                                      hasMipmaps, origin());
  if (newTextureProxy == nullptr) {
    return nullptr;
  }
  if (copyContent) {
    newTextureProxy->instantiate();
    auto newTexture = newTextureProxy->getTexture();
    if (newTexture == nullptr) {
      return nullptr;
    }
    context->gpu()->copyRenderTargetToTexture(renderTarget.get(), newTexture.get(),
                                              Rect::MakeWH(width(), height()), Point::Zero());
  }
  return newTextureProxy;
}

std::shared_ptr<RenderTargetProxy> RenderTargetProxy::makeRenderTargetProxy(
    bool copyContent) const {
  auto textureProxy = makeTextureProxy(copyContent);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<TextureRenderTargetProxy>(
      new TextureRenderTargetProxy(std::move(textureProxy), format(), sampleCount(), false));
}
}  // namespace tgfx
