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
#include "RasterizedImage.h"
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

std::shared_ptr<TextureProxy> CodecImage::lockTextureProxy(const TPArgs& args) const {
  auto tempGenerator = generator;
  auto scaleWidth = static_cast<int>(roundf(static_cast<float>(width()) * args.drawScale));
  auto scaleHeight = static_cast<int>(roundf(static_cast<float>(height()) * args.drawScale));
  if (scaleWidth < generator->width() && scaleHeight < generator->height()) {
    tempGenerator = ScaledImageGenerator::MakeFrom(getCodec(), scaleWidth, scaleHeight);
  }
  return args.context->proxyProvider()->createTextureProxy(tempGenerator, args.mipmapped,
                                                           args.renderFlags);
}
}  // namespace tgfx
