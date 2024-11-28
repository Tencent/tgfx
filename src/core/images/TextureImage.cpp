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
std::shared_ptr<Image> TextureImage::Wrap(std::shared_ptr<TextureProxy> textureProxy) {
  TRACE_EVENT;
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto contextID = textureProxy->getContext()->uniqueID();
  auto textureImage =
      std::shared_ptr<TextureImage>(new TextureImage(std::move(textureProxy), contextID));
  textureImage->weakThis = textureImage;
  return textureImage;
}

TextureImage::TextureImage(std::shared_ptr<TextureProxy> textureProxy, uint32_t contextID)
    : ResourceImage(textureProxy->getUniqueKey()), textureProxy(std::move(textureProxy)),
      contextID(contextID) {
}

BackendTexture TextureImage::getBackendTexture(Context* context, ImageOrigin* origin) const {
  TRACE_EVENT;
  if (context == nullptr || context->uniqueID() != contextID) {
    return {};
  }
  context->flush();
  auto texture = textureProxy->getTexture();
  if (texture == nullptr) {
    return {};
  }
  if (origin != nullptr) {
    *origin = textureProxy->origin();
  }
  return texture->getBackendTexture();
}

std::shared_ptr<Image> TextureImage::makeTextureImage(Context* context) const {
  TRACE_EVENT;
  if (context == nullptr || context->uniqueID() != contextID) {
    return nullptr;
  }
  return std::static_pointer_cast<Image>(weakThis.lock());
}

std::shared_ptr<TextureProxy> TextureImage::onLockTextureProxy(const TPArgs& args) const {
  TRACE_EVENT;
  if (args.context == nullptr || args.context->uniqueID() != contextID) {
    return nullptr;
  }
  return textureProxy;
}
}  // namespace tgfx
