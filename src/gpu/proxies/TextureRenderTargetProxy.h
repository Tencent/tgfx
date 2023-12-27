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

#pragma once

#include "RenderTargetProxy.h"

namespace tgfx {
class TextureRenderTargetProxy : public RenderTargetProxy {
 public:
  int width() const override {
    return textureProxy->width();
  }

  int height() const override {
    return textureProxy->height();
  }

  ImageOrigin origin() const override {
    return textureProxy->origin();
  }

  PixelFormat format() const override {
    return _format;
  }

  int sampleCount() const override {
    return renderTarget ? renderTarget->sampleCount() : _sampleCount;
  }

  Context* getContext() const override {
    return textureProxy->getContext();
  }

  bool externallyOwned() const override {
    return _externallyOwned;
  }

  std::shared_ptr<TextureProxy> getTextureProxy() const override {
    return textureProxy;
  }

 protected:
  std::shared_ptr<RenderTarget> onMakeRenderTarget() override;

 private:
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  PixelFormat _format = PixelFormat::RGBA_8888;
  int _sampleCount = 0;
  bool _externallyOwned = false;

  TextureRenderTargetProxy(std::shared_ptr<TextureProxy> textureProxy, PixelFormat format,
                           int sampleCount, bool externallyOwned);

  friend class RenderTargetProxy;
};
}  // namespace tgfx
