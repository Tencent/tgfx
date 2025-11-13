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

#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/Image.h"

namespace tgfx {
/**
 * PictureImage is an image that draws a Picture.
 */
class PictureImage : public Image {
 public:
  PictureImage(std::shared_ptr<Picture> picture, int width, int height,
               const Matrix* matrix = nullptr, bool mipmapped = false,
               std::shared_ptr<ColorSpace> colorSpace = ColorSpace::SRGB());

  ~PictureImage() override;

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  bool isAlphaOnly() const override {
    return false;
  }

  bool hasMipmaps() const override {
    return mipmapped;
  }

  std::shared_ptr<ColorSpace> colorSpace() const override {
    return _colorSpace;
  }

  std::shared_ptr<Picture> picture = nullptr;
  Matrix* matrix = nullptr;

 protected:
  Type type() const override {
    return Type::Picture;
  }

  std::shared_ptr<Image> onMakeScaled(int newWidth, int newHeight,
                                      const SamplingOptions& sampling) const override;

  std::shared_ptr<Image> onMakeMipmapped(bool enabled) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                      const SamplingArgs& samplingArgs,
                                                      const Matrix* uvMatrix) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

  bool drawPicture(std::shared_ptr<RenderTargetProxy> renderTarget, uint32_t renderFlags,
                   const Matrix* extraMatrix) const;

 private:
  int _width = 0;
  int _height = 0;
  bool mipmapped = false;
  std::shared_ptr<ColorSpace> _colorSpace = ColorSpace::SRGB();
};
}  // namespace tgfx
