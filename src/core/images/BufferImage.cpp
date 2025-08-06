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
#include "core/images/CodecImage.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
std::shared_ptr<Image> Image::MakeFrom(std::shared_ptr<ImageBuffer> buffer) {
  if (buffer == nullptr) {
    return nullptr;
  }
  std::shared_ptr<Image> image = std::make_shared<BufferImage>(std::move(buffer), false);
  image->weakThis = image;
  return image->makeRasterized();
}

BufferImage::BufferImage(std::shared_ptr<ImageBuffer> buffer, bool mipmapped)
    : ResourceImage(mipmapped), imageBuffer(std::move(buffer)) {
}

std::shared_ptr<TextureProxy> BufferImage::lockTextureProxy(const TPArgs& args) const {
  return args.context->proxyProvider()->createTextureProxy(imageBuffer, args.mipmapped);
}

std::shared_ptr<Image> BufferImage::onMakeMipmapped(bool mipmapped) const {
  auto image = std::make_shared<BufferImage>(imageBuffer, mipmapped);
  image->weakThis = image;
  return image;
}

std::shared_ptr<Image> BufferImage::onMakeScaled(int newWidth, int newHeight,
                                                 const SamplingOptions& sampling) const {
  if (imageBuffer->isPixelBuffer() && newWidth < imageBuffer->width() &&
      newHeight < imageBuffer->height()) {
    auto codec = PixelBufferCodec::Make(std::static_pointer_cast<PixelBuffer>(imageBuffer));
    auto image = std::make_shared<CodecImage>(std::move(codec), newWidth, newHeight, mipmapped);
    image->weakThis = image;
    return image;
  }
  return ResourceImage::onMakeScaled(newWidth, newHeight, sampling);
}

}  // namespace tgfx
