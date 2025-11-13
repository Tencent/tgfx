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

#include "GeneratorImage.h"
#include "core/DataSource.h"

namespace tgfx {
/**
 * DecodedImage wraps an image source that can asynchronously decode ImageBuffers.
 */
class DecodedImage : public PixelImage {
 public:
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<ImageGenerator> generator,
                                         bool tryHardware, bool asyncDecoding, bool mipmapped);

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  bool isAlphaOnly() const override {
    return _alphaOnly;
  }

  std::shared_ptr<ColorSpace> colorSpace() const override {
    return _colorSpace;
  }

 protected:
  Type type() const override {
    return Type::Decoded;
  }

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

  std::shared_ptr<Image> onMakeMipmapped(bool mipmapped) const override;

 private:
  int _width = 0;
  int _height = 0;
  bool _alphaOnly = false;
  std::shared_ptr<DataSource<ImageBuffer>> source = nullptr;
  std::shared_ptr<ColorSpace> _colorSpace = ColorSpace::SRGB();

  DecodedImage(int width, int height, bool alphaOnly,
               std::shared_ptr<DataSource<ImageBuffer>> source, bool mipmapped,
               std::shared_ptr<ColorSpace> colorSpace = ColorSpace::SRGB());
};
}  // namespace tgfx
