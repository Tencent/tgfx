/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "DefaultTextureProxy.h"

namespace tgfx {
DefaultTextureProxy::DefaultTextureProxy(int width, int height, PixelFormat pixelFormat,
                                         bool mipmapped, ImageOrigin origin,
                                         std::shared_ptr<ColorSpace> colorSpace)
    : TextureProxy(width, height, pixelFormat, mipmapped, origin),
      _colorSpace(std::move(colorSpace)) {
}

std::shared_ptr<TextureView> DefaultTextureProxy::getTextureView() const {
  if (resource == nullptr) {
    resource = onMakeTexture(context);
    if (resource != nullptr && !uniqueKey.empty()) {
      resource->assignUniqueKey(uniqueKey);
    }
  }
  return std::static_pointer_cast<TextureView>(resource);
}

std::shared_ptr<TextureView> DefaultTextureProxy::onMakeTexture(Context* context) const {
  return TextureView::MakeFormat(context, _backingStoreWidth, _backingStoreHeight, _format,
                                 _mipmapped, _origin, _colorSpace);
}

}  // namespace tgfx
