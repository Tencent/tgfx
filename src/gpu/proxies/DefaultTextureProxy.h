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

#include "gpu/BackingFit.h"
#include "gpu/proxies/TextureProxy.h"

namespace tgfx {
class DefaultTextureProxy : public TextureProxy {
 public:
  int backingStoreWidth() const override {
    return _backingStoreWidth;
  }

  int backingStoreHeight() const override {
    return _backingStoreHeight;
  }

  std::shared_ptr<Texture> getTexture() const override;

 protected:
  int _backingStoreWidth = 0;
  int _backingStoreHeight = 0;

  DefaultTextureProxy(int width, int height, PixelFormat pixelFormat, bool mipmapped = false,
                      ImageOrigin origin = ImageOrigin::TopLeft,
                      BackingFit backingFit = BackingFit::Exact);

  virtual std::shared_ptr<Texture> onMakeTexture(Context* context) const;

 private:
  UniqueKey uniqueKey = {};

  friend class ProxyProvider;
};
}  // namespace tgfx
