/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "TextAtlasUploadTask.h"
#include "gpu/Texture.h"

namespace tgfx {

TextAtlasUploadTask::TextAtlasUploadTask(UniqueKey uniqueKey,
                                         std::shared_ptr<DataSource<ImageBuffer>> source,
                                         std::shared_ptr<TextureProxy> textureProxy,
                                         Point atlasOffset)
    : ResourceTask(std::move(uniqueKey)), source(std::move(source)),
      textureProxy(std::move(textureProxy)), atlasOffset(atlasOffset) {
}

bool TextAtlasUploadTask::execute(Context*) {
  if (source == nullptr || textureProxy == nullptr) {
    return false;
  }
  auto imageBuffer = source->getData();
  if (imageBuffer == nullptr) {
    LOGE("TextAtlasUploadTask::execute() imageBuffer is nullptr!");
    return nullptr;
  }
  auto result = imageBuffer->onUploadTexture(textureProxy->getTexture(), atlasOffset);
  if (!result) {
    LOGE("TextAtlasUploadTask::execute() Failed to upload the texture!");
  } else {
    // Free the image source immediately to reduce memory pressure.
    source = nullptr;
  }
  return true;
}

}  // namespace tgfx
