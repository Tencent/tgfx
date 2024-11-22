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

#include "TextureCreateTask.h"
#include "gpu/Texture.h"
#include "core/utils/Profiling.h"

namespace tgfx {
std::shared_ptr<TextureCreateTask> TextureCreateTask::MakeFrom(UniqueKey uniqueKey, int width,
                                                               int height, PixelFormat format,
                                                               bool mipmapped, ImageOrigin origin) {
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  return std::shared_ptr<TextureCreateTask>(
      new TextureCreateTask(std::move(uniqueKey), width, height, format, mipmapped, origin));
}

TextureCreateTask::TextureCreateTask(UniqueKey uniqueKey, int width, int height, PixelFormat format,
                                     bool mipmapped, ImageOrigin origin)
    : ResourceTask(std::move(uniqueKey)), width(width), height(height), format(format),
      mipmapped(mipmapped), origin(origin) {
}

std::shared_ptr<Resource> TextureCreateTask::onMakeResource(Context* context) {
  TRACY_ZONE_SCOPED_N("TextureCreateTask::onMakeResource");
  auto texture = Texture::MakeFormat(context, width, height, format, mipmapped, origin);
  if (texture == nullptr) {
    LOGE("TextureCreateTask::onMakeResource() Failed to create the texture!");
  }
  return texture;
}

}  // namespace tgfx
