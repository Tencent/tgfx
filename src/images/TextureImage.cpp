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

#include "TextureImage.h"

namespace tgfx {
std::shared_ptr<Image> TextureImage::MakeFrom(std::shared_ptr<TextureProxy> textureProxy) {
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto textureImage = std::shared_ptr<TextureImage>(new TextureImage(std::move(textureProxy)));
  textureImage->weakThis = textureImage;
  return textureImage;
}

TextureImage::TextureImage(std::shared_ptr<TextureProxy> textureProxy)
    : ResourceImage(textureProxy->getResourceKey()), textureProxy(std::move(textureProxy)) {
}

BackendTexture TextureImage::getBackendTexture(Context* context) const {
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

std::shared_ptr<Image> TextureImage::makeTextureImage(Context* context) const {
  if (textureProxy->getContext() == context) {
    return std::static_pointer_cast<Image>(weakThis.lock());
  }
  return nullptr;
}

std::shared_ptr<TextureProxy> TextureImage::onLockTextureProxy(Context* context, uint32_t) const {
  if (textureProxy->getContext() != context) {
    return nullptr;
  }
  return textureProxy;
}
}  // namespace tgfx