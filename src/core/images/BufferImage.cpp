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

#include "BufferImage.h"
#include "core/PixelBuffer.h"
#include "core/PixelBufferCodec.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
std::shared_ptr<Image> Image::MakeFrom(std::shared_ptr<ImageBuffer> buffer) {
  if (buffer == nullptr) {
    return nullptr;
  }
  auto image = std::make_shared<BufferImage>(UniqueKey::Make(), std::move(buffer));
  image->weakThis = image;
  return image;
}

BufferImage::BufferImage(UniqueKey uniqueKey, std::shared_ptr<ImageBuffer> buffer)
    : ResourceImage(std::move(uniqueKey)), imageBuffer(std::move(buffer)) {
}

std::shared_ptr<Image> BufferImage::onMakeScaled(int newWidth, int newHeight,
                                                 const SamplingOptions& sampling) const {
  if (imageBuffer->isPixelBuffer() && newWidth < imageBuffer->width() &&
      newHeight < imageBuffer->height()) {
    auto pixelBuffer = std::dynamic_pointer_cast<PixelBuffer>(imageBuffer);
    auto codec = PixelBufferCodec::Make(pixelBuffer, newWidth, newHeight);
    return Image::MakeFrom(codec);
  }
  return ResourceImage::onMakeScaled(newWidth, newHeight, sampling);
}

std::shared_ptr<TextureProxy> BufferImage::onLockTextureProxy(const TPArgs& args,
                                                              const UniqueKey& key) const {
  return args.context->proxyProvider()->createTextureProxy(key, imageBuffer, args.mipmapped,
                                                           args.renderFlags);
}
}  // namespace tgfx
