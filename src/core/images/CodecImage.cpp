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

#include "CodecImage.h"
#include <algorithm>
#include "DecodedImage.h"
#include "core/ScaledImageGenerator.h"
#include "core/utils/NextCacheScaleLevel.h"
#include "gpu/ProxyProvider.h"
#include "gpu/TPArgs.h"

namespace tgfx {
CodecImage::CodecImage(std::shared_ptr<ImageCodec> codec, int width, int height, bool mipmapped)
    : GeneratorImage(std::move(codec), mipmapped), _width(width), _height(height) {
}

std::shared_ptr<ImageCodec> CodecImage::getCodec() const {
  return std::static_pointer_cast<ImageCodec>(generator);
}

float CodecImage::getRasterizedScale(float drawScale) const {
  return NextCacheScaleLevel(drawScale);
}

std::shared_ptr<Image> CodecImage::onMakeScaled(int newWidth, int newHeight,
                                                const SamplingOptions& sampling) const {
  if (newWidth <= generator->width() && newHeight <= generator->height()) {
    auto image = std::make_shared<CodecImage>(getCodec(), newWidth, newHeight, mipmapped);
    image->weakThis = image;
    return image;
  }
  return PixelImage::onMakeScaled(newWidth, newHeight, sampling);
}

std::shared_ptr<Image> CodecImage::onMakeMipmapped(bool enabled) const {
  auto image = std::make_shared<CodecImage>(getCodec(), _width, _height, enabled);
  image->weakThis = image;
  return image;
}

ISize CodecImage::getRasterizedSize(float scale) const {
  auto codec = getCodec();
  auto codecScale = static_cast<float>(width()) * scale / static_cast<float>(codec->width());
  auto [scaleWidth, scaleHeight] = codec->getScaledDimensions(codecScale);
  // Rounding the shorter edge of a nonempty image can produce zero at small scales.
  scaleWidth = std::max(1, scaleWidth);
  scaleHeight = std::max(1, scaleHeight);
  if (scaleWidth < generator->width() && scaleHeight < generator->height()) {
    return ISize::Make(scaleWidth, scaleHeight);
  }
  return ISize::Make(generator->width(), generator->height());
}

std::shared_ptr<ImageGenerator> CodecImage::getGeneratorForScale(float scale) const {
  auto size = getRasterizedSize(scale);
  if (size.width < generator->width() && size.height < generator->height()) {
    return ScaledImageGenerator::MakeFrom(getCodec(), size.width, size.height);
  }
  return generator;
}

std::shared_ptr<Image> CodecImage::onMakeDecoded(Context*, bool tryHardware) const {
  auto decodeGenerator = getGeneratorForScale(1.0f);
  // A main-thread codec can wrap its native image directly, but its scaled generator reads and
  // downsamples the pixels. Defer that work until the texture is needed.
  auto asyncDecoding = decodeGenerator == generator || generator->asyncSupport();
  return DecodedImage::MakeFrom(std::move(decodeGenerator), tryHardware, asyncDecoding, mipmapped,
                                ISize::Make(width(), height()));
}

std::shared_ptr<TextureProxy> CodecImage::lockTextureProxy(const TPArgs& args) const {
  return args.context->proxyProvider()->createTextureProxy(getGeneratorForScale(args.drawScale),
                                                           args.mipmapped, args.renderFlags);
}
}  // namespace tgfx
