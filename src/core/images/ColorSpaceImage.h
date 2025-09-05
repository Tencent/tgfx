/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "tgfx/core/Image.h"

namespace tgfx {
class ColorSpaceImage : public Image {
 public:
  ColorSpaceImage(std::shared_ptr<ColorSpace> colorSpace, std::shared_ptr<Image> image)
      : _colorSpace(std::move(colorSpace)), _sourceImage(std::move(image)) {
  }

  ~ColorSpaceImage() override = default;

  int width() const override {
    return _sourceImage->width();
  }

  int height() const override {
    return _sourceImage->height();
  }

  bool isAlphaOnly() const override {
    return _sourceImage->isAlphaOnly();
  }

  bool hasMipmaps() const override {
    return _sourceImage->hasMipmaps();
  }

  bool isFullyDecoded() const override {
    return _sourceImage->isFullyDecoded();
  }

  bool isTextureBacked() const override {
    return _sourceImage->isTextureBacked();
  }

  std::shared_ptr<ColorSpace> colorSpace() const override {
    return _colorSpace;
  }

  std::shared_ptr<Image> makeColorSpace(std::shared_ptr<ColorSpace> colorSpace) const override {
    auto result = std::make_shared<ColorSpaceImage>(*this);
    result->_colorSpace = colorSpace;
    result->weakThis = result;
    return result;
  }

  std::shared_ptr<Image> makeTextureImage(Context* context) const override {
    auto image = _sourceImage->makeTextureImage(context);
    return image->makeColorSpace(_colorSpace);
  }

  BackendTexture getBackendTexture(Context* context, ImageOrigin* origin) const override {
    return _sourceImage->getBackendTexture(context, origin);
  }

  std::shared_ptr<Image> makeRasterized() const override {
    auto image = _sourceImage->makeRasterized();
    return image->makeColorSpace(_colorSpace);
  }

 protected:
  Type type() const override {
    return Type::ColorSpace;
  }

  std::shared_ptr<Image> onMakeDecoded(Context* context, bool tryHardware) const override {
    auto image = _sourceImage->onMakeDecoded(context, tryHardware);
    return image->makeColorSpace(_colorSpace);
  }

  std::shared_ptr<Image> onMakeMipmapped(bool enabled) const override {
    auto image = _sourceImage->onMakeMipmapped(enabled);
    return image->makeColorSpace(_colorSpace);
  }

  std::shared_ptr<Image> onMakeSubset(const Rect& subset) const override {
    auto image = _sourceImage->onMakeSubset(subset);
    return image->makeColorSpace(_colorSpace);
  }

  std::shared_ptr<Image> onMakeOriented(Orientation orientation) const override {
    auto image = _sourceImage->onMakeOriented(orientation);
    return image->makeColorSpace(_colorSpace);
  }

  std::shared_ptr<Image> onMakeWithFilter(std::shared_ptr<ImageFilter> filter, Point* offset,
                                          const Rect* clipRect) const override {
    auto image = _sourceImage->onMakeWithFilter(filter, offset, clipRect);
    return image->makeColorSpace(_colorSpace);
  }

  std::shared_ptr<Image> onMakeScaled(int newWidth, int newHeight,
                                      const SamplingOptions& sampling) const override {
    auto scaledImage = _sourceImage->onMakeScaled(newWidth, newHeight, sampling);
    return scaledImage->makeColorSpace(_colorSpace);
  }

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                      const SamplingArgs& samplingArgs,
                                                      const Matrix* uvMatrix) const override;

 private:
  std::shared_ptr<ColorSpace> _colorSpace;
  std::shared_ptr<Image> _sourceImage;
};
}  // namespace tgfx
