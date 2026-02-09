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

#pragma once

#include "gpu/tasks/AtlasUploadTask.h"

namespace tgfx {
class ImageBuffer;

struct DirectUploadCell {
  std::shared_ptr<ImageBuffer> imageBuffer = nullptr;
  int offsetX = 0;
  int offsetY = 0;
};

class WebAtlasUploadTask final : public AtlasUploadTask {
 public:
  explicit WebAtlasUploadTask(std::shared_ptr<TextureProxy> proxy);

  void addCell(BlockAllocator* allocator, std::shared_ptr<ImageCodec> codec,
               const Point& atlasOffset) override;

  void upload(Context* context) override;

 private:
  std::vector<DirectUploadCell> directUploadCells = {};
};
}  // namespace tgfx
