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
#include "core/PixelBuffer.h"
#include "gpu/DrawingManager.h"
#include "gpu/Gpu.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
std::shared_ptr<RenderTargetProxy> RenderTargetProxy::MakeFrom(
    Context* context, const BackendRenderTarget& backendRenderTarget, ImageOrigin origin) {
  if (context == nullptr) {
    return nullptr;
  }
  return context->proxyProvider()->wrapBackendRenderTarget(backendRenderTarget, origin);
}

std::shared_ptr<RenderTargetProxy> RenderTargetProxy::MakeFrom(Context* context,
                                                               const BackendTexture& backendTexture,
                                                               int sampleCount, ImageOrigin origin,
                                                               bool adopted) {
  if (context == nullptr) {
    return nullptr;
  }
  auto textureProxy = context->proxyProvider()->wrapBackendTexture(backendTexture, origin, adopted);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto textureSampler = TextureSampler::MakeFrom(context, backendTexture);
  return context->proxyProvider()->createRenderTargetProxy(std::move(textureProxy),
                                                           textureSampler->format, sampleCount);
}

std::shared_ptr<RenderTargetProxy> RenderTargetProxy::MakeFrom(Context* context,
                                                               HardwareBufferRef hardwareBuffer,
                                                               int sampleCount) {
  if (context == nullptr) {
    return nullptr;
  }
  auto format = HardwareBufferGetPixelFormat(hardwareBuffer);
  if (format == PixelFormat::Unknown) {
    return nullptr;
  }
  auto pixelBuffer = PixelBuffer::MakeFrom(hardwareBuffer);
  if (pixelBuffer == nullptr) {
    return nullptr;
  }
  auto textureProxy =
      context->proxyProvider()->createTextureProxy({}, std::move(pixelBuffer), false);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return context->proxyProvider()->createRenderTargetProxy(std::move(textureProxy), format,
                                                           sampleCount);
}

std::shared_ptr<RenderTargetProxy> RenderTargetProxy::Make(Context* context, int width, int height,
                                                           PixelFormat format, int sampleCount,
                                                           bool mipMapped, ImageOrigin origin) {
  if (context == nullptr) {
    return nullptr;
  }
  if (!context->caps()->isFormatRenderable(format)) {
    return nullptr;
  }
  auto proxyProvider = context->proxyProvider();
  auto textureProxy =
      proxyProvider->createTextureProxy({}, width, height, format, mipMapped, origin);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return proxyProvider->createRenderTargetProxy(std::move(textureProxy), format, sampleCount);
}

RenderTargetProxy::RenderTargetProxy(int width, int height, PixelFormat format, int sampleCount,
                                     ImageOrigin origin)
    : _width(width), _height(height), _format(format), _sampleCount(sampleCount), _origin(origin) {
}

bool RenderTargetProxy::isTextureBacked() const {
  return getTextureProxy() != nullptr;
}

std::shared_ptr<Texture> RenderTargetProxy::getTexture() const {
  auto textureProxy = getTextureProxy();
  return textureProxy ? textureProxy->getTexture() : nullptr;
}

std::shared_ptr<RenderTarget> RenderTargetProxy::getRenderTarget() const {
  return Resource::Get<RenderTarget>(context, resourceKey);
}

std::shared_ptr<TextureProxy> RenderTargetProxy::makeTextureProxy() const {
  auto context = getContext();
  auto textureProxy = getTextureProxy();
  auto hasMipmaps = textureProxy && textureProxy->hasMipmaps();
  return context->proxyProvider()->createTextureProxy({}, width(), height(), format(), hasMipmaps,
                                                      origin());
}

std::shared_ptr<RenderTargetProxy> RenderTargetProxy::makeRenderTargetProxy() const {
  auto textureProxy = makeTextureProxy();
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return context->proxyProvider()->createRenderTargetProxy(std::move(textureProxy), format(),
                                                           sampleCount());
}
}  // namespace tgfx
