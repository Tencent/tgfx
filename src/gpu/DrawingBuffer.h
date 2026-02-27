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

#include <vector>
#include "core/utils/BlockAllocator.h"
#include "core/utils/SlidingWindowTracker.h"
#include "gpu/tasks/AtlasUploadTask.h"
#include "gpu/tasks/RenderTask.h"
#include "gpu/tasks/ResourceTask.h"

namespace tgfx {
class DrawingBuffer {
 public:
  explicit DrawingBuffer(Context* context);

  /**
   * Returns true if there are no tasks in the drawing buffer.
   */
  bool empty() const;

  /**
   * Resets the drawing buffer to be empty.
   */
  void reset();

  /**
   * Returns the unique ID of this drawing buffer.
   */
  uint32_t uniqueID() const {
    return _uniqueID;
  }

  /**
   * Returns the current generation number of this drawing buffer.
   */
  uint64_t generation() const {
    return _generation;
  }

  /**
   * Encodes all pending render tasks into GPU commands and returns a CommandBuffer ready for
   * submission to the GPU. Returns nullptr if there are no tasks to encode.
   */
  std::shared_ptr<CommandBuffer> encode();

 private:
  Context* context = nullptr;
  uint32_t _uniqueID = 0;
  uint64_t _generation = 0;
  BlockAllocator drawingAllocator = {};
  SlidingWindowTracker drawingMaxValueTracker = {10};
  BlockAllocator vertexAllocator = {};
  SlidingWindowTracker vertexMaxValueTracker = {10};
  std::vector<PlacementPtr<ResourceTask>> resourceTasks = {};
  std::vector<PlacementPtr<RenderTask>> renderTasks = {};
  std::vector<PlacementPtr<AtlasUploadTask>> atlasTasks = {};

  friend class DrawingManager;
};
}  // namespace tgfx
