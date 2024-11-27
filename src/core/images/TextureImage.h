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

#include "core/images/ResourceImage.h"

namespace tgfx {
/**
 * TextureImage wraps an existing texture proxy.
 */
class TextureImage : public ResourceImage {
 public:
  /**
   * Creates an Image wraps the existing TextureProxy, returns nullptr if textureProxy is nullptr.
   */
  static std::shared_ptr<Image> Wrap(std::shared_ptr<TextureProxy> textureProxy);

  int width() const override {
    return textureProxy->width();
  }

  int height() const override {
    return textureProxy->height();
  }

  bool isAlphaOnly() const override {
    return textureProxy->isAlphaOnly();
  }

  bool isYUV() const override {
    return textureProxy->isYUV();
  }

  bool hasMipmaps() const override {
    return textureProxy->hasMipmaps();
  }

  bool isTextureBacked() const override {
    return true;
  }

  bool isFlat() const override {
    return !textureProxy->isYUV();
  }

  BackendTexture getBackendTexture(Context* context, ImageOrigin* origin = nullptr) const override;

  std::shared_ptr<Image> makeTextureImage(Context* context,
                                          const SamplingOptions& sampling = {}) const override;

 protected:
  std::shared_ptr<Image> onMakeMipmapped(bool) const override {
    return nullptr;
  }

  std::shared_ptr<TextureProxy> onLockTextureProxy(const TPArgs& args) const override;

 private:
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  uint32_t contextID = 0;

  TextureImage(std::shared_ptr<TextureProxy> textureProxy, uint32_t contextID);
};
}  // namespace tgfx
