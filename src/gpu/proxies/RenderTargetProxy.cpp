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

#include "RenderTargetProxy.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/proxies/ExternalRenderTargetProxy.h"

namespace tgfx {
std::shared_ptr<RenderTargetProxy> RenderTargetProxy::MakeFrom(
    Context* context, const BackendRenderTarget& backendRenderTarget, ImageOrigin origin,
    std::shared_ptr<ColorSpace> colorSpace) {
  auto renderTarget =
      RenderTarget::MakeFrom(context, backendRenderTarget, origin, std::move(colorSpace));
  if (renderTarget == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<ExternalRenderTargetProxy>(
      new ExternalRenderTargetProxy(std::move(renderTarget)));
}

std::shared_ptr<RenderTargetProxy> RenderTargetProxy::MakeFallback(
    Context* context, int width, int height, bool alphaOnly, int sampleCount, bool mipmapped,
    ImageOrigin origin, std::shared_ptr<ColorSpace> colorSpace, BackingFit backingFit) {
  if (context == nullptr) {
    return nullptr;
  }
  auto alphaRenderable = context->caps()->isFormatRenderable(PixelFormat::ALPHA_8);
  auto format = alphaOnly && alphaRenderable ? PixelFormat::ALPHA_8 : PixelFormat::RGBA_8888;
  return context->proxyProvider()->createRenderTargetProxy({}, width, height, format, sampleCount,
                                                           mipmapped, origin, std::move(colorSpace),
                                                           backingFit, 0);
}

std::shared_ptr<TextureProxy> RenderTargetProxy::makeTextureProxy(int width, int height) const {
  auto textureProxy = asTextureProxy();
  auto hasMipmaps = textureProxy && textureProxy->hasMipmaps();
  return getContext()->proxyProvider()->createTextureProxy(
      {}, width, height, format(), hasMipmaps, origin(), colorSpace(), BackingFit::Exact, 0);
}

std::shared_ptr<RenderTargetProxy> RenderTargetProxy::makeRenderTargetProxy(int width,
                                                                            int height) const {
  return getContext()->proxyProvider()->createRenderTargetProxy(
      {}, width, height, format(), sampleCount(), false, ImageOrigin::TopLeft, colorSpace(),
      BackingFit::Exact, 0);
}

Matrix RenderTargetProxy::getOriginTransform() const {
  if (origin() == ImageOrigin::TopLeft) {
    return Matrix::I();
  }
  auto textureProxy = asTextureProxy();
  auto offset = textureProxy ? textureProxy->backingStoreHeight() : height();
  auto result = Matrix::MakeScale(1.0f, -1.0f);
  result.postTranslate(0.0f, static_cast<float>(offset));
  return result;
}

}  // namespace tgfx
