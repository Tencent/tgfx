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
#include "gpu/ProxyProvider.h"

namespace tgfx {

CodecImage::CodecImage(int width, int height, UniqueKey uniqueKey,
                       std::shared_ptr<ImageCodec> codec)
    : GeneratorImage(std::move(uniqueKey), std::move(codec)), _width(width), _height(height) {
}

std::shared_ptr<ImageCodec> CodecImage::getCodec() const {
  return std::static_pointer_cast<ImageCodec>(generator);
}

std::shared_ptr<Image> CodecImage::onMakeScaled(int newWidth, int newHeight,
                                                const SamplingOptions& sampling) const {
  if (newWidth <= width() && newHeight <= height()) {
    auto image = std::make_shared<CodecImage>(newWidth, newHeight, uniqueKey, getCodec());
    image->weakThis = image;
    return image;
  }
  return ResourceImage::onMakeScaled(newWidth, newHeight, sampling);
}

std::shared_ptr<TextureProxy> CodecImage::onLockTextureProxy(const TPArgs& args,
                                                             const UniqueKey& key) const {
  auto tempGenerator = generator;
  if (width() != generator->width() || height() != generator->height()) {
    tempGenerator = ScaledImageGenerator::MakeFrom(width(), height(), getCodec());
  }
  return args.context->proxyProvider()->createTextureProxy(key, tempGenerator, args.mipmapped,
                                                           args.renderFlags);
}
}  // namespace tgfx
