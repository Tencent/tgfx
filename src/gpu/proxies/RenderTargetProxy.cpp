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
#include "core/PixelBuffer.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
class BackendRenderTargetWrapper : public RenderTargetProxy {
 public:
  explicit BackendRenderTargetWrapper(std::shared_ptr<RenderTarget> renderTarget)
      : renderTarget(std::move(renderTarget)) {
  }

  Context* getContext() const override {
    return renderTarget->getContext();
  }

  int width() const override {
    return renderTarget->width();
  }

  int height() const override {
    return renderTarget->height();
  }

  PixelFormat format() const override {
    return renderTarget->format();
  }

  int sampleCount() const override {
    return renderTarget->sampleCount();
  }

  ImageOrigin origin() const override {
    return renderTarget->origin();
  }

  bool externallyOwned() const override {
    return true;
  }

  std::shared_ptr<Texture> getTexture() const override {
    return nullptr;
  }

  std::shared_ptr<RenderTarget> getRenderTarget() const override {
    return renderTarget;
  }

 private:
  std::shared_ptr<RenderTarget> renderTarget = nullptr;
};

std::shared_ptr<RenderTargetProxy> RenderTargetProxy::MakeFrom(
    Context* context, const BackendRenderTarget& backendRenderTarget, ImageOrigin origin) {
  auto renderTarget = RenderTarget::MakeFrom(context, backendRenderTarget, origin);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  return std::make_shared<BackendRenderTargetWrapper>(std::move(renderTarget));
}

std::shared_ptr<RenderTargetProxy> RenderTargetProxy::MakeFallback(Context* context, int width,
                                                                   int height, bool isAlphaOnly,
                                                                   int sampleCount, bool mipmapped,
                                                                   ImageOrigin origin,
                                                                   TextureSizePolicy sizePolicy) {
  if (context == nullptr) {
    return nullptr;
  }
  auto alphaRenderable = context->caps()->isFormatRenderable(PixelFormat::ALPHA_8);
  auto format = isAlphaOnly && alphaRenderable ? PixelFormat::ALPHA_8 : PixelFormat::RGBA_8888;
  return context->proxyProvider()->createRenderTargetProxy({}, width, height, format, sampleCount,
                                                           mipmapped, origin, sizePolicy);
}

std::shared_ptr<TextureProxy> RenderTargetProxy::makeTextureProxy(int width, int height) const {
  auto textureProxy = asTextureProxy();
  auto hasMipmaps = textureProxy && textureProxy->hasMipmaps();
  return getContext()->proxyProvider()->createTextureProxy({}, width, height, format(), hasMipmaps,
                                                           origin());
}

std::shared_ptr<RenderTargetProxy> RenderTargetProxy::makeRenderTargetProxy(int width,
                                                                            int height) const {
  return getContext()->proxyProvider()->createRenderTargetProxy({}, width, height, format(),
                                                                sampleCount());
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
