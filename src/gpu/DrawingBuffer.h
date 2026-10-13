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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <vector>
#include "core/utils/BlockAllocator.h"
#include "core/utils/SlidingWindowTracker.h"
#include "gpu/tasks/AtlasUploadTask.h"
#include "gpu/tasks/RenderTask.h"
#include "gpu/tasks/ResourceTask.h"

namespace tgfx {

class Drawable;
class RenderTargetProxy;
class Window;

class DrawingBuffer {
 public:
  explicit DrawingBuffer(Context* context);

  /**
   * Returns true if there are no tasks or presentation requests in the drawing buffer. A buffer
   * that only carries presentation requests is not empty: it must be flushable and submittable
   * so that a presentation registered after the frame's rendering was submitted is still
   * delivered.
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

  /**
   * Wires the frames this batch uses into the upcoming submission: backends whose render
   * submission must synchronize with the frame acquisition (Vulkan's imageAvailable semaphore
   * wait) attach that synchronization here, regardless of whether a presentation has been
   * registered for the frame. Called right before the command buffer is submitted.
   */
  void attachFrameUsages(Context* context);

  /**
   * Schedules the presentation requests registered for this batch onto the upcoming submission,
   * so each presentation is encoded/ordered with the command buffer of the batch it was
   * registered on. Called right before the command buffer is submitted.
   */
  void schedulePresents(Context* context);

  /**
   * Marks the used frames as submitted, delivers the batch's presentation requests after the
   * command buffer submission, and marks the presented frames as presented.
   */
  void deliverFrames(Context* context);

 private:
  // A presentation request registered on this batch (through Context::present() or the automatic
  // window path). The proxy carries the backend presentation; the drawable is null for purely
  // proxy-backed (automatic-path) requests.
  struct PendingPresentation {
    std::shared_ptr<Drawable> drawable = nullptr;
    std::shared_ptr<RenderTargetProxy> proxy = nullptr;
  };

  // The frames whose rendering commands this batch carries. Entries are strong: the buffer keeps
  // its frames alive until it is submitted, so dropping a Recording never discards rendering
  // that still runs. The frames hold their windows weakly (see Drawable) and their proxies carry
  // the presentation, so no device reference cycle is formed even though the Window holds its
  // device strongly (see Window::_device).
  std::vector<std::shared_ptr<Drawable>> frameUsages = {};

  // The automatic window frames this batch uses, as declared by their surfaces (drawing passes as
  // well as readback/copy transfers without a registered presentation), so the submission wires
  // the frame acquisition synchronization for every frame it uses.
  std::vector<std::shared_ptr<RenderTargetProxy>> windowUsages = {};

  // The presentation requests registered on this batch. Entries are strong for the same reason
  // as frameUsages: a dropped Recording still has its registered presentations delivered.
  std::vector<PendingPresentation> presentables = {};

  Context* context = nullptr;
  uint32_t _uniqueID = 0;
  uint64_t _generation = 0;
  BlockAllocator drawingAllocator = {};
  SlidingWindowTracker drawingMaxValueTracker = {10};
  BlockAllocator vertexAllocator = {};
  SlidingWindowTracker vertexMaxValueTracker = {10};
  BlockAllocator instanceAllocator = {};
  SlidingWindowTracker instanceMaxValueTracker = {10};
  std::vector<PlacementPtr<ResourceTask>> resourceTasks = {};
  std::vector<PlacementPtr<RenderTask>> renderTasks = {};
  std::vector<PlacementPtr<AtlasUploadTask>> atlasTasks = {};

  friend class DrawingManager;
};
}  // namespace tgfx
