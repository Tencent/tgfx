/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "TextureCopyTask.h"
#include "core/utils/Log.h"

namespace tgfx {
TextureCopyTask::TextureCopyTask(BlockAllocator* allocator, std::shared_ptr<TextureProxy> source,
                                 std::shared_ptr<TextureProxy> dest)
    : RenderTask(allocator), source(std::move(source)), dest(std::move(dest)) {
}

void TextureCopyTask::execute(CommandEncoder* encoder) {
  auto sourceView = source->getTextureView();
  auto destView = dest->getTextureView();
  if (sourceView == nullptr || destView == nullptr) {
    LOGE("TextureCopyTask::execute() Failed to get the source or dest texture view!");
    return;
  }
  auto sourceTexture = sourceView->getTexture();
  auto rect = Rect::MakeWH(sourceTexture->width(), sourceTexture->height());
  encoder->copyTextureToTexture(sourceTexture, rect, destView->getTexture(), Point::Zero());
}
}  // namespace tgfx
