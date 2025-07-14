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

class FlattenTextureProxy : public TextureProxy {
 public:
  int width() const override {
    return source->width();
  }

  int height() const override {
    return source->height();
  }

  int backingStoreWidth() const override {
    return source->backingStoreWidth();
  }

  int backingStoreHeight() const override {
    return source->backingStoreHeight();
  }

  ImageOrigin origin() const override {
    return ImageOrigin::TopLeft;
  }

  bool hasMipmaps() const override {
    return source->hasMipmaps();
  }

  bool isAlphaOnly() const override {
    return source->isAlphaOnly();
  }

  bool isFlatten() const override {
    return true;
  }

 protected:
  std::shared_ptr<Texture> onMakeTexture(Context* context) const override;

 private:
  UniqueKey flattenTextureKey = {};
  std::shared_ptr<TextureProxy> source = nullptr;

  FlattenTextureProxy(UniqueKey flattenTextureKey, std::shared_ptr<TextureProxy> source);

  friend class ProxyProvider;
};
}  // namespace tgfx
