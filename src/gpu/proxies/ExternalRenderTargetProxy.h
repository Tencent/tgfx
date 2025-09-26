/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
class ExternalRenderTargetProxy : public RenderTargetProxy {
 public:
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

  std::shared_ptr<TextureView> getTextureView() const override {
    return nullptr;
  }

  std::shared_ptr<RenderTarget> getRenderTarget() const override {
    return renderTarget;
  }

  std::shared_ptr<ColorSpace> getColorSpace() const override {
    return colorSpace;
  }

 private:
  std::shared_ptr<RenderTarget> renderTarget = nullptr;
  std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB();

  explicit ExternalRenderTargetProxy(std::shared_ptr<RenderTarget> renderTarget,
                                     std::shared_ptr<ColorSpace> colorSpace)
      : renderTarget(std::move(renderTarget)), colorSpace(std::move(colorSpace)) {
  }

  friend class RenderTargetProxy;
};
}  // namespace tgfx
