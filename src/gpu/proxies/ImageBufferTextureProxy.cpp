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

#include "ImageBufferTextureProxy.h"

namespace tgfx {
ImageBufferTextureProxy::ImageBufferTextureProxy(ProxyProvider* provider,
                                                 std::shared_ptr<ImageBuffer> imageBuffer,
                                                 bool mipMapped)
    : TextureProxy(provider), imageBuffer(std::move(imageBuffer)), mipMapped(mipMapped) {
}

int ImageBufferTextureProxy::width() const {
  return texture ? texture->width() : imageBuffer->width();
}

int ImageBufferTextureProxy::height() const {
  return texture ? texture->height() : imageBuffer->height();
}

bool ImageBufferTextureProxy::hasMipmaps() const {
  return texture ? texture->getSampler()->hasMipmaps() : mipMapped;
}

std::shared_ptr<Texture> ImageBufferTextureProxy::onMakeTexture(Context* context) {
  if (imageBuffer == nullptr) {
    return nullptr;
  }
  auto texture = Texture::MakeFrom(context, imageBuffer, mipMapped);
  if (texture != nullptr) {
    imageBuffer = nullptr;
  }
  return texture;
}
}  // namespace tgfx
