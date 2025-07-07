/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
class DefaultTextureProxy : public TextureProxy {
 public:
  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  ImageOrigin origin() const override {
    return bitFields.origin;
  }

  bool hasMipmaps() const override {
    return bitFields.mipmapped;
  }

  bool isAlphaOnly() const override {
    return bitFields.isAlphaOnly;
  }

  bool externallyOwned() const override {
    return bitFields.externallyOwned;
  }

  std::shared_ptr<Texture> getTexture() const override;

 private:
  int _width = 0;
  int _height = 0;

  struct {
    ImageOrigin origin : 2;
    bool mipmapped : 1;
    bool isAlphaOnly : 1;
    bool externallyOwned : 1;
  } bitFields = {};

  DefaultTextureProxy(UniqueKey uniqueKey, int width, int height, bool mipmapped, bool isAlphaOnly,
                      ImageOrigin origin = ImageOrigin::TopLeft, bool externallyOwned = false);

  friend class ProxyProvider;
};
}  // namespace tgfx
