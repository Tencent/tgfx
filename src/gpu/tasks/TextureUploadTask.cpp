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

#include "TextureUploadTask.h"
#include "gpu/Texture.h"

namespace tgfx {
std::unique_ptr<TextureUploadTask> TextureUploadTask::MakeFrom(
    UniqueKey uniqueKey, std::shared_ptr<DataSource<ImageBuffer>> source, bool mipmapped) {
  if (source == nullptr) {
    return nullptr;
  }
  return std::unique_ptr<TextureUploadTask>(
      new TextureUploadTask(std::move(uniqueKey), std::move(source), mipmapped));
}

TextureUploadTask::TextureUploadTask(UniqueKey uniqueKey,
                                     std::shared_ptr<DataSource<ImageBuffer>> source,
                                     bool mipmapped)
    : ResourceTask(std::move(uniqueKey)), source(std::move(source)), mipmapped(mipmapped) {
}

std::shared_ptr<Resource> TextureUploadTask::onMakeResource(Context* context) {
  if (source == nullptr) {
    return nullptr;
  }
  auto imageBuffer = source->getData();
  if (imageBuffer == nullptr) {
    LOGE("TextureUploadTask::onMakeResource() Failed to decode the image!");
    return nullptr;
  }
  auto texture = Texture::MakeFrom(context, imageBuffer, mipmapped);
  if (texture == nullptr) {
    LOGE("TextureUploadTask::onMakeResource() Failed to upload the texture!");
  } else {
    // Free the image source immediately to reduce memory pressure.
    source = nullptr;
  }
  return texture;
}
}  // namespace tgfx
