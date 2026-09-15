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
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {
TextureUploadTask::TextureUploadTask(std::shared_ptr<ResourceProxy> proxy,
                                     std::shared_ptr<DataSource<ImageBuffer>> source,
                                     bool mipmapped)
    : ResourceTask(proxy), source(std::move(source)), mipmapped(mipmapped) {
  // The call sites always hand us a TextureProxy; keep a typed reference so execute() can end
  // its pending-upload intent.
  textureProxy = std::static_pointer_cast<TextureProxy>(std::move(proxy));
}

bool TextureUploadTask::execute(Context* context) {
  auto success = ResourceTask::execute(context);
  // The upload intent ends here either way: on success the view is instantiated and the
  // pending flag no longer matters; on failure the view stays null forever and the flag must
  // be cleared so later draws go back to refusing the un-instantiated view.
  if (textureProxy != nullptr) {
    textureProxy->_hasPendingUpload = false;
  }
  return success;
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
