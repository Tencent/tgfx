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

#include "WebAtlasUploadTask.h"
#include "WebImageBuffer.h"
#include "core/AtlasTypes.h"
#include "core/utils/BlockAllocator.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {
PlacementPtr<AtlasUploadTask> AtlasUploadTask::Make(BlockAllocator* allocator,
                                                    std::shared_ptr<TextureProxy> proxy) {
  return allocator->make<WebAtlasUploadTask>(std::move(proxy));
}

WebAtlasUploadTask::WebAtlasUploadTask(std::shared_ptr<TextureProxy> proxy)
    : AtlasUploadTask(std::move(proxy)) {
}

void WebAtlasUploadTask::addCell(BlockAllocator* allocator, std::shared_ptr<ImageCodec> codec,
                                 const Point& atlasOffset) {
  if (!codec->asyncSupport() && hardwarePixels == nullptr) {
    auto imageBuffer = codec->makeBuffer(false);
    if (imageBuffer != nullptr) {
      auto padding = Plot::CellPadding;
      auto offsetX = static_cast<int>(atlasOffset.x) - padding;
      auto offsetY = static_cast<int>(atlasOffset.y) - padding;
      DirectUploadCell cell;
      cell.imageBuffer = std::move(imageBuffer);
      cell.offsetX = offsetX;
      cell.offsetY = offsetY;
      directUploadCells.push_back(std::move(cell));
      return;
    }
  }
  AtlasUploadTask::addCell(allocator, std::move(codec), atlasOffset);
}

void WebAtlasUploadTask::upload(Context* context) {
  auto textureView = textureProxy->getTextureView();
  if (textureView == nullptr) {
    return;
  }
  for (auto& cell : directUploadCells) {
    auto webBuffer = std::static_pointer_cast<WebImageBuffer>(cell.imageBuffer);
    webBuffer->uploadToTexture(textureView->getTexture(), cell.offsetX, cell.offsetY);
  }
  directUploadCells.clear();
  AtlasUploadTask::upload(context);
}
}  // namespace tgfx
