/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "gpu/resources/TextureView.h"
#include "inspect/InspectorMark.h"

namespace tgfx {
TextureUploadTask::TextureUploadTask(std::shared_ptr<ResourceProxy> proxy,
                                     std::shared_ptr<DataSource<ImageBuffer>> source,
                                     bool mipmapped)
    : ResourceTask(std::move(proxy)), source(std::move(source)), mipmapped(mipmapped) {
}

std::shared_ptr<Resource> TextureUploadTask::onMakeResource(Context* context) {
  TASK_MARK(tgfx::inspect::OpTaskType::TextureUploadTask);
  ATTRIBUTE_NAME("mipmaped", mipmapped);
  if (source == nullptr) {
    return nullptr;
  }
  auto imageBuffer = source->getData();
  if (imageBuffer == nullptr) {
    LOGE("TextureUploadTask::onMakeResource() Failed to decode the image!");
    return nullptr;
  }
  auto textureView = TextureView::MakeFrom(context, imageBuffer, mipmapped);
  if (textureView == nullptr) {
    LOGE("TextureUploadTask::onMakeResource() Failed to upload the texture view!");
  } else {
    // Free the image source immediately to reduce memory pressure.
    source = nullptr;
  }
  return textureView;
}
}  // namespace tgfx
