/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "TextureCreateTask.h"
#include "gpu/Texture.h"

namespace tgfx {
TextureCreateTask::TextureCreateTask(std::shared_ptr<ResourceProxy> proxy, int width, int height,
                                     PixelFormat format, bool mipmapped, ImageOrigin origin)
    : ResourceTask(std::move(proxy)), width(width), height(height), format(format),
      mipmapped(mipmapped), origin(origin) {
}

std::shared_ptr<Resource> TextureCreateTask::onMakeResource(Context* context) {
  auto texture = Texture::MakeFormat(context, width, height, format, mipmapped, origin);
  if (texture == nullptr) {
    LOGE("TextureCreateTask::onMakeResource() Failed to create the texture!");
  }
  return texture;
}

}  // namespace tgfx
