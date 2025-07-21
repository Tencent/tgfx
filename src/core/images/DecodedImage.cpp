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

namespace tgfx {
std::shared_ptr<Image> DecodedImage::MakeFrom(UniqueKey uniqueKey,
                                              std::shared_ptr<ImageGenerator> generator,
                                              bool tryHardware, bool asyncDecoding) {
  if (generator == nullptr) {
    return nullptr;
  }
  auto width = generator->width();
  auto height = generator->height();
  auto alphaOnly = generator->isAlphaOnly();
  auto source = ImageSource::MakeFrom(std::move(generator), tryHardware, asyncDecoding);
  auto image = std::shared_ptr<DecodedImage>(
      new DecodedImage(std::move(uniqueKey), width, height, alphaOnly, std::move(source)));
  image->weakThis = image;
  return image;
}

DecodedImage::DecodedImage(UniqueKey uniqueKey, int width, int height, bool alphaOnly,
                           std::shared_ptr<DataSource<ImageBuffer>> source)
    : ResourceImage(std::move(uniqueKey)), _width(width), _height(height), _alphaOnly(alphaOnly),
      source(std::move(source)) {
}

std::shared_ptr<TextureProxy> DecodedImage::onLockTextureProxy(const TPArgs& args,
                                                               const UniqueKey& key) const {
  return args.context->proxyProvider()->createTextureProxy(key, source, _width, _height, _alphaOnly,
                                                           args.mipmapped, args.renderFlags);
}
}  // namespace tgfx
