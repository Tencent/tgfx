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

#include "DrawingBuffer.h"
#include "core/utils/UniqueID.h"
#include "gpu/GlobalCache.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
// We set the maxBlockSize to 2MB because allocating blocks that are too large can cause memory
// fragmentation and slow down allocation. It may also increase the application's memory usage due
// to pre-allocation optimizations on some platforms.
DrawingBuffer::DrawingBuffer(Context* context)
    : context(context), _uniqueID(UniqueID::Next()), drawingAllocator(1 << 14, 1 << 21),
      vertexAllocator(1 << 14, 1 << 21), instanceAllocator(1 << 14, 1 << 21) {
  DEBUG_ASSERT(context != nullptr);
}

std::shared_ptr<CommandBuffer> DrawingBuffer::encode() {
  DEBUG_ASSERT(!empty());
  {
    for (auto& task : resourceTasks) {
      task->execute(context);
      task = nullptr;
    }
  }
  for (auto& task : atlasTasks) {
    task->upload(context);
    task = nullptr;
  }
  auto commandEncoder = context->gpu()->createCommandEncoder();
  {
    for (auto& task : renderTasks) {
      task->execute(commandEncoder.get());
      task = nullptr;
    }
  }
  vertexMaxValueTracker.addValue(vertexAllocator.size());
  instanceMaxValueTracker.addValue(instanceAllocator.size());
  drawingMaxValueTracker.addValue(drawingAllocator.size());
  auto commandBuffer = commandEncoder->finish();
  context->globalCache()->resetUniformBuffer();
  return commandBuffer;
}

void DrawingBuffer::attachFrameUsages(Context* context) {
  for (auto& proxy : windowUsages) {
    proxy->onAttachSubmission(context);
  }
}

void DrawingBuffer::schedulePresents(Context* context) {
  for (auto& proxy : presentables) {
    proxy->onSchedulePresentation(context);
  }
}

void DrawingBuffer::deliverFrames(Context* context) {
  for (auto& proxy : presentables) {
    proxy->onPresentFrame(context);
  }
  presentables.clear();
  windowUsages.clear();
}

bool DrawingBuffer::empty() const {
  // Window frame usages are intentionally not part of the emptiness check: they always come with
  // the task that uses the frame. Presentations are kept so that a batch whose drawing pass was
  // opened on a window frame still delivers that frame.
  return resourceTasks.empty() && renderTasks.empty() && atlasTasks.empty() && presentables.empty();
}

void DrawingBuffer::reset() {
  renderTasks.clear();
  resourceTasks.clear();
  atlasTasks.clear();
  windowUsages.clear();
  presentables.clear();
  vertexAllocator.clear(vertexMaxValueTracker.getMaxValue());
  instanceAllocator.clear(instanceMaxValueTracker.getMaxValue());
  drawingAllocator.clear(drawingMaxValueTracker.getMaxValue());
  _generation++;
}

}  // namespace tgfx
