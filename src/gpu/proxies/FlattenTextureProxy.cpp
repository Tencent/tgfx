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

#include "FlattenTextureProxy.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
FlattenTextureProxy::FlattenTextureProxy(UniqueKey uniqueKey, std::shared_ptr<TextureProxy> source)
    : TextureProxy(std::move(uniqueKey)), source(std::move(source)) {
}

std::shared_ptr<Texture> FlattenTextureProxy::getTexture() const {
  auto texture = Resource::Find<Texture>(context, handle.key());
  return texture ? texture : source->getTexture();
}
}  // namespace tgfx
