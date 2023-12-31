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

#include "gpu/proxies/TextureProxy.h"

namespace tgfx {
class DeferredTextureProxy : public TextureProxy {
 public:
  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  ImageOrigin origin() const override {
    return _origin;
  }

  bool hasMipmaps() const override;

 protected:
  std::shared_ptr<Texture> onMakeTexture(Context* context) override;

 private:
  int _width = 0;
  int _height = 0;
  PixelFormat _format = PixelFormat::RGBA_8888;
  bool mipMapped = false;
  ImageOrigin _origin = ImageOrigin::TopLeft;

  DeferredTextureProxy(ProxyProvider* provider, int width, int height, PixelFormat format,
                       bool mipMapped = false, ImageOrigin origin = ImageOrigin::TopLeft);

  friend class ProxyProvider;
};
}  // namespace tgfx
