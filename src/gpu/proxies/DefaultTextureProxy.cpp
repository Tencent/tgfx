/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
DefaultTextureProxy::DefaultTextureProxy(UniqueKey uniqueKey, int width, int height, bool mipmapped,
                                         bool isAlphaOnly, ImageOrigin origin, bool externallyOwned)
    : TextureProxy(std::move(uniqueKey)), _width(width), _height(height) {
  bitFields.origin = origin;
  bitFields.mipmapped = mipmapped;
  bitFields.isAlphaOnly = isAlphaOnly;
  bitFields.externallyOwned = externallyOwned;
}

std::shared_ptr<Texture> DefaultTextureProxy::getTexture() const {
  return Resource::Find<Texture>(context, handle.key());
}
}  // namespace tgfx
