/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
std::shared_ptr<Image> BufferImage::MakeFrom(std::shared_ptr<ImageBuffer> buffer) {
  if (buffer == nullptr) {
    return nullptr;
  }
  auto image =
      std::shared_ptr<BufferImage>(new BufferImage(ResourceKey::NewWeak(), std::move(buffer)));
  image->weakThis = image;
  return image;
}

BufferImage::BufferImage(ResourceKey resourceKey, std::shared_ptr<ImageBuffer> buffer)
    : TextureImage(std::move(resourceKey)), imageBuffer(std::move(buffer)) {
}

std::shared_ptr<TextureProxy> BufferImage::onLockTextureProxy(Context* context,
                                                              const ResourceKey& key,
                                                              bool mipmapped,
                                                              uint32_t renderFlags) const {
  return context->proxyProvider()->createTextureProxy(key, imageBuffer, mipmapped, renderFlags);
}
}  // namespace tgfx
