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
  std::shared_ptr<TextureProxy> getTextureProxy() const override {
    return textureProxy;
  }

 private:
  std::shared_ptr<TextureProxy> textureProxy = nullptr;

  TextureRenderTargetProxy(UniqueKey uniqueKey, std::shared_ptr<TextureProxy> textureProxy,
                           PixelFormat format, int sampleCount)
      : RenderTargetProxy(std::move(uniqueKey), textureProxy->width(), textureProxy->height(),
                          format, sampleCount, textureProxy->origin()),
        textureProxy(std::move(textureProxy)) {
  }

  friend class ProxyProvider;
};
}  // namespace tgfx
