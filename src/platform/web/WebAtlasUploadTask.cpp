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

namespace tgfx {

class DirectCellUploadTask : public CellUploadTask {
 public:
  DirectCellUploadTask(std::shared_ptr<ImageBuffer> imageBuffer, int offsetX, int offsetY)
      : imageBuffer(std::move(imageBuffer)), offsetX(offsetX), offsetY(offsetY) {
  }

  void upload(std::shared_ptr<Texture> texture, CommandQueue*) override {
    auto webBuffer = std::static_pointer_cast<WebImageBuffer>(imageBuffer);
    webBuffer->uploadToTexture(std::move(texture), offsetX, offsetY);
  }

 private:
  std::shared_ptr<ImageBuffer> imageBuffer = nullptr;
  int offsetX = 0;
  int offsetY = 0;
};

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
      cellTasks.push_back(
          std::make_shared<DirectCellUploadTask>(std::move(imageBuffer), offsetX, offsetY));
      return;
    }
  }
  AtlasUploadTask::addCell(allocator, std::move(codec), atlasOffset);
}
}  // namespace tgfx
