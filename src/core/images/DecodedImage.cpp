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

#include "DecodedImage.h"
#include "BufferImage.h"
#include "core/ImageSource.h"
#include "gpu/ProxyProvider.h"
#include "gpu/TPArgs.h"

namespace tgfx {
std::shared_ptr<Image> DecodedImage::MakeFrom(std::shared_ptr<ImageGenerator> generator,
                                              bool tryHardware, bool asyncDecoding,
                                              bool mipmapped) {
  if (generator == nullptr) {
    return nullptr;
  }
  auto width = generator->width();
  auto height = generator->height();
  auto alphaOnly = generator->isAlphaOnly();
  auto source = ImageSource::MakeFrom(generator, tryHardware, asyncDecoding);
  auto colorSpace = generator->colorSpace();
  auto image = std::shared_ptr<DecodedImage>(
      new DecodedImage(width, height, alphaOnly, std::move(source), mipmapped, colorSpace));
  image->weakThis = image;
  return image;
}

DecodedImage::DecodedImage(int width, int height, bool alphaOnly,
                           std::shared_ptr<DataSource<ImageBuffer>> source, bool mipmapped,
                           std::shared_ptr<ColorSpace> colorSpace)
    : PixelImage(mipmapped), _width(width), _height(height), _alphaOnly(alphaOnly),
      source(std::move(source)), _colorSpace(std::move(colorSpace)) {
}

std::shared_ptr<TextureProxy> DecodedImage::lockTextureProxy(const TPArgs& args) const {
  return args.context->proxyProvider()->createTextureProxy(source, _width, _height, _alphaOnly,
                                                           args.mipmapped);
}

std::shared_ptr<Image> DecodedImage::onMakeMipmapped(bool mipmapped) const {
  auto image = std::shared_ptr<DecodedImage>(
      new DecodedImage(_width, _height, _alphaOnly, source, mipmapped, colorSpace()));
  image->weakThis = image;
  return image;
}

}  // namespace tgfx
