/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "gpu/proxies/TextureProxy.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class CellDecodeTask;
class ImageBuffer;

struct DirectUploadCell {
  std::shared_ptr<ImageBuffer> imageBuffer = nullptr;
  int offsetX = 0;
  int offsetY = 0;
};

struct SyncDecodedCell {
  void* pixels = nullptr;
  ImageInfo info = {};
  int offsetX = 0;
  int offsetY = 0;
};

class AtlasUploadTask {
 public:
  explicit AtlasUploadTask(std::shared_ptr<TextureProxy> proxy);

  ~AtlasUploadTask();

  void addCell(BlockAllocator* allocator, std::shared_ptr<ImageCodec> codec,
               const Point& atlasOffset);

  void upload(Context* context);

 private:
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  ImageInfo hardwareInfo = {};
  void* hardwarePixels = nullptr;
  std::vector<std::shared_ptr<CellDecodeTask>> tasks = {};
  std::vector<DirectUploadCell> directUploadCells = {};
  std::vector<SyncDecodedCell> syncDecodedCells = {};
};
}  // namespace tgfx
