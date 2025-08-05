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
#include "gpu/ProxyProvider.h"

namespace tgfx {
std::shared_ptr<Image> Image::MakeFrom(std::shared_ptr<ImageBuffer> buffer) {
  if (buffer == nullptr) {
    return nullptr;
  }
  std::shared_ptr<Image> image = std::make_shared<BufferImage>(std::move(buffer), false);
  image->weakThis = image;
  image = image->makeRasterized();
  return image;
}

BufferImage::BufferImage(std::shared_ptr<ImageBuffer> buffer, bool mipmap)
    : ResourceImage(mipmap), imageBuffer(std::move(buffer)) {
}

std::shared_ptr<TextureProxy> BufferImage::onLockTextureProxy(const TPArgs& args) const {
  return args.context->proxyProvider()->createTextureProxy({}, imageBuffer, args.mipmapped,
                                                           args.renderFlags);
}

std::shared_ptr<Image> BufferImage::onCloneWith(bool mipmap) const {
  auto image = std::make_shared<BufferImage>(imageBuffer, mipmap);
  image->weakThis = image;
  return image;
}

}  // namespace tgfx
