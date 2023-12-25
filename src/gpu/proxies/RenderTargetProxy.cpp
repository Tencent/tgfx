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
  auto textureProxy = context->proxyProvider()->wrapTexture(std::move(texture));
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<TextureRenderTargetProxy>(
      new TextureRenderTargetProxy(std::move(textureProxy), sampleCount));
}

std::shared_ptr<RenderTargetProxy> RenderTargetProxy::Make(Context* context, int width, int height,
                                                           tgfx::PixelFormat format,
                                                           int sampleCount, bool mipMapped) {
  if (context == nullptr) {
    return nullptr;
  }
  auto textureProxy =
      context->proxyProvider()->createTextureProxy(width, height, format, mipMapped);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<TextureRenderTargetProxy>(
      new TextureRenderTargetProxy(std::move(textureProxy), sampleCount));
}

RenderTargetProxy::RenderTargetProxy(std::shared_ptr<RenderTarget> renderTarget)
    : renderTarget(std::move(renderTarget)) {
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
}  // namespace tgfx
