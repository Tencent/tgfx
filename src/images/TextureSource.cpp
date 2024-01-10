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

#include "TextureSource.h"

namespace tgfx {

TextureSource::TextureSource(std::shared_ptr<TextureProxy> textureProxy)
    : ImageSource(textureProxy->getUniqueKey()), textureProxy(std::move(textureProxy)) {
}

BackendTexture TextureSource::getBackendTexture(Context* context) const {
  if (context == nullptr) {
    return {};
  }
  context->flush();
  auto texture = textureProxy->getTexture();
  if (texture == nullptr) {
    return {};
  }
  return texture->getBackendTexture();
}

std::shared_ptr<ImageSource> TextureSource::makeTextureSource(Context* context) const {
  if (textureProxy->getContext() == context) {
    return std::static_pointer_cast<ImageSource>(weakThis.lock());
  }
  return nullptr;
}

std::shared_ptr<TextureProxy> TextureSource::onMakeTextureProxy(Context* context, uint32_t) const {
  if (textureProxy->getContext() != context) {
    return nullptr;
  }
  return textureProxy;
}
}  // namespace tgfx