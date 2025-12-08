/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include "tgfx/core/Image.h"

namespace tgfx {
/**
 * TextureImage wraps an existing texture proxy.
 */
class TextureImage : public Image {
 public:
  /**
   * Creates an Image wraps the existing TextureProxy, returns nullptr if textureProxy is nullptr.
   */
  static std::shared_ptr<Image> Wrap(std::shared_ptr<TextureProxy> textureProxy,
                                     std::shared_ptr<ColorSpace> colorSpace);

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

  BackendTexture getBackendTexture(Context* context, ImageOrigin* origin) const override;

  std::shared_ptr<Image> makeTextureImage(Context* context) const override;

  std::shared_ptr<Image> makeRasterized() const override;

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return _colorSpace;
  }

 protected:
  Type type() const override {
    return Type::Texture;
  }

  std::shared_ptr<Image> onMakeMipmapped(bool) const override {
    return nullptr;
  }

  std::shared_ptr<Image> onMakeScaled(int newWidth, int newHeight,
                                      const SamplingOptions& sampling) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                      const SamplingArgs& samplingArgs,
                                                      const Matrix* uvMatrix) const override;

 private:
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  uint32_t contextID = 0;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;

  TextureImage(std::shared_ptr<TextureProxy> textureProxy, uint32_t contextID,
               std::shared_ptr<ColorSpace> colorSpace);
};
}  // namespace tgfx
