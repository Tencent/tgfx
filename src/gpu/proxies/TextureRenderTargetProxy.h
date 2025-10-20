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

#pragma once

#include "DefaultTextureProxy.h"
#include "RenderTargetProxy.h"

namespace tgfx {
class TextureRenderTargetProxy : public DefaultTextureProxy,
                                 public RenderTargetProxy,
                                 public std::enable_shared_from_this<TextureRenderTargetProxy> {
 public:
  Context* getContext() const override {
    return context;
  }

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  PixelFormat format() const override {
    return _format;
  }

  int sampleCount() const override {
    return _sampleCount;
  }

  ImageOrigin origin() const override {
    return _origin;
  }

  bool externallyOwned() const override {
    return _externallyOwned;
  }

  std::shared_ptr<TextureProxy> asTextureProxy() const override {
    return std::const_pointer_cast<TextureRenderTargetProxy>(shared_from_this());
  }

  std::shared_ptr<RenderTargetProxy> asRenderTargetProxy() const override {
    return std::const_pointer_cast<TextureRenderTargetProxy>(shared_from_this());
  }

  std::shared_ptr<TextureView> getTextureView() const override {
    return DefaultTextureProxy::getTextureView();
  }

  std::shared_ptr<RenderTarget> getRenderTarget() const override;

  std::shared_ptr<ColorSpace> gamutColorSpace() const override {
    return _gamutColorSpace;
  }

 protected:
  int _sampleCount = 1;
  bool _externallyOwned = false;

  TextureRenderTargetProxy(int width, int height, PixelFormat format, int sampleCount,
                           bool mipmapped = false, ImageOrigin origin = ImageOrigin::TopLeft,
                           bool externallyOwned = false,
                           std::shared_ptr<ColorSpace> colorSpace = nullptr);

  std::shared_ptr<TextureView> onMakeTexture(Context* context) const override;

  friend class ProxyProvider;
};
}  // namespace tgfx
