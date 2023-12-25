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

#include "ImageGeneratorTextureProxy.h"

namespace tgfx {
ImageGeneratorTextureProxy::ImageGeneratorTextureProxy(ProxyProvider* provider,
                                                       std::shared_ptr<ImageGeneratorTask> task,
                                                       bool mipMapped)
    : TextureProxy(provider), task(std::move(task)), mipMapped(mipMapped) {
}

int ImageGeneratorTextureProxy::width() const {
  return texture ? texture->width() : task->imageWidth();
}

int ImageGeneratorTextureProxy::height() const {
  return texture ? texture->height() : task->imageHeight();
}

bool ImageGeneratorTextureProxy::hasMipmaps() const {
  return texture ? texture->getSampler()->hasMipmaps() : mipMapped;
}

std::shared_ptr<Texture> ImageGeneratorTextureProxy::onMakeTexture(Context* context) {
  if (task == nullptr) {
    return nullptr;
  }
  auto buffer = task->getBuffer();
  if (buffer == nullptr) {
    return nullptr;
  }
  auto texture = Texture::MakeFrom(context, buffer, mipMapped);
  if (texture != nullptr) {
    task = nullptr;
  }
  return texture;
}
}  // namespace tgfx
