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

#include "DeferredTextureProxy.h"

namespace tgfx {
DeferredTextureProxy::DeferredTextureProxy(ProxyProvider* provider, int width, int height,
                                           PixelFormat format, ImageOrigin origin, bool mipMapped)
    : TextureProxy(provider), _width(width), _height(height), format(format), origin(origin),
      mipMapped(mipMapped) {
}

int DeferredTextureProxy::width() const {
  return _width;
}

int DeferredTextureProxy::height() const {
  return _height;
}

bool DeferredTextureProxy::hasMipmaps() const {
  return texture ? texture->getSampler()->hasMipmaps() : mipMapped;
}

std::shared_ptr<Texture> DeferredTextureProxy::onMakeTexture(Context* context) {
  if (context == nullptr) {
    return nullptr;
  }
  return Texture::MakeFormat(context, width(), height(), format, origin, mipMapped);
}
}  // namespace tgfx
