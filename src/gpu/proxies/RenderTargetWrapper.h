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
class RenderTargetWrapper : public RenderTargetProxy {
 public:
  int width() const override {
    return renderTarget->width();
  }

  int height() const override {
    return renderTarget->height();
  }

  ImageOrigin origin() const override {
    return renderTarget->origin();
  }

  PixelFormat format() const override {
    return renderTarget->format();
  }

  int sampleCount() const override {
    return renderTarget->sampleCount();
  }

  Context* getContext() const override {
    return renderTarget->getContext();
  }

  bool externallyOwned() const override {
    return true;
  }

  std::shared_ptr<TextureProxy> getTextureProxy() const override;

 protected:
  std::shared_ptr<RenderTarget> onMakeRenderTarget() override;

 private:
  explicit RenderTargetWrapper(std::shared_ptr<RenderTarget> renderTarget);

  friend class RenderTargetProxy;
};
}  // namespace tgfx
