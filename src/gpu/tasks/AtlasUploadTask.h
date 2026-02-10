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

#include "core/utils/PlacementPtr.h"
#include "gpu/proxies/TextureProxy.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class BlockAllocator;
class CommandQueue;
class Texture;

class CellUploadTask {
 public:
  virtual ~CellUploadTask() = default;

  virtual void upload(std::shared_ptr<Texture> texture, CommandQueue* queue) = 0;

  virtual void cancel() {
  }
};

class AtlasUploadTask {
 public:
  /**
   * Creates a new AtlasUploadTask instance. On the Web platform, returns a WebAtlasUploadTask that
   * supports direct texture uploading from canvas elements.
   */
  static PlacementPtr<AtlasUploadTask> Make(BlockAllocator* allocator,
                                            std::shared_ptr<TextureProxy> proxy);

  explicit AtlasUploadTask(std::shared_ptr<TextureProxy> proxy);

  virtual ~AtlasUploadTask();

  virtual void addCell(BlockAllocator* allocator, std::shared_ptr<ImageCodec> codec,
                       const Point& atlasOffset);

  void upload(Context* context);

 protected:
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  ImageInfo hardwareInfo = {};
  void* hardwarePixels = nullptr;
  std::vector<std::shared_ptr<CellUploadTask>> cellTasks = {};
};
}  // namespace tgfx
