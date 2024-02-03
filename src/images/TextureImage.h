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
#include "images/RasterImage.h"

namespace tgfx {
/**
 * TextureImage wraps an existing texture proxy.
 */
class TextureImage : public RasterImage {
 public:
  /**
   * Creates an Image wraps the existing TextureProxy, returns nullptr if textureProxy is nullptr.
   */
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<TextureProxy> textureProxy);

  int width() const override {
    return textureProxy->width();
  }

  int height() const override {
    return textureProxy->height();
  }

  bool isAlphaOnly() const override {
    return textureProxy->isAlphaOnly();
  }

  bool hasMipmaps() const override {
    return textureProxy->hasMipmaps();
  }

  bool isTextureBacked() const override {
    return true;
  }

  BackendTexture getBackendTexture(Context* context, ImageOrigin* origin = nullptr) const override;

  std::shared_ptr<Image> makeTextureImage(Context* context) const override;

 protected:
  std::shared_ptr<Image> onMakeMipmapped(bool enabled) const override;

  std::shared_ptr<TextureProxy> onLockTextureProxy(Context* context, const ResourceKey& key,
                                                   bool mipmapped,
                                                   uint32_t renderFlags) const override;

 private:
  std::shared_ptr<TextureProxy> textureProxy = nullptr;

  explicit TextureImage(std::shared_ptr<TextureProxy> textureProxy);
};
}  // namespace tgfx
