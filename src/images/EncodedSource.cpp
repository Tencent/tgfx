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

#include "EncodedSource.h"
#include "AsyncSource.h"

namespace tgfx {
EncodedSource::EncodedSource(ResourceKey resourceKey, std::shared_ptr<ImageGenerator> generator,
                             bool mipMapped)
    : ImageSource(std::move(resourceKey)), generator(std::move(generator)), mipMapped(mipMapped) {
}

std::shared_ptr<ImageSource> EncodedSource::onMakeDecoded(Context* context) const {
  if (context != nullptr) {
    if (context->proxyProvider()->hasResourceProxy(resourceKey) ||
        context->resourceCache()->hasResource(resourceKey)) {
      return nullptr;
    }
  }
  return std::shared_ptr<AsyncSource>(new AsyncSource(resourceKey, generator, mipMapped));
}

std::shared_ptr<ImageSource> EncodedSource::onMakeMipMapped() const {
  return std::shared_ptr<EncodedSource>(new EncodedSource(ResourceKey::NewWeak(), generator, true));
}

std::shared_ptr<TextureProxy> EncodedSource::onMakeTextureProxy(Context* context,
                                                                uint32_t renderFlags) const {
  return context->proxyProvider()->createTextureProxy(resourceKey, generator, mipMapped,
                                                      renderFlags);
}
}  // namespace tgfx
