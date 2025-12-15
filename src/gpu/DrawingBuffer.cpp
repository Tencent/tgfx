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

#include "DrawingBuffer.h"
#include "core/utils/UniqueID.h"
#include "gpu/GlobalCache.h"
#include "inspect/InspectorMark.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
// We set the maxBlockSize to 2MB because allocating blocks that are too large can cause memory
// fragmentation and slow down allocation. It may also increase the application's memory usage due
// to pre-allocation optimizations on some platforms.
DrawingBuffer::DrawingBuffer(Context* context)
    : context(context), _uniqueID(UniqueID::Next()), drawingAllocator(1 << 14, 1 << 21),
      vertexAllocator(1 << 14, 1 << 21) {
  DEBUG_ASSERT(context != nullptr);
}

std::shared_ptr<CommandBuffer> DrawingBuffer::encode() {
  DEBUG_ASSERT(!empty());
  TASK_MARK(tgfx::inspect::OpTaskType::Flush);
  auto clock = tgfx::Clock();
  {
    TASK_MARK(tgfx::inspect::OpTaskType::ResourceTask);
    for (auto& task : resourceTasks) {
      task->execute(context);
      task = nullptr;
    }
    clock.mark("resourceTasks");
  }
  for (auto& task : atlasTasks) {
    task->upload(context);
    task = nullptr;
  }
  clock.mark("atlasTasks");
  auto commandEncoder = context->gpu()->createCommandEncoder();
  {
    TASK_MARK(tgfx::inspect::OpTaskType::RenderTask);
    for (auto& task : renderTasks) {
      task->execute(commandEncoder.get());
      task = nullptr;
    }
  }
  LOGI("DrawingBuffer::encode cost: %ld resourceTasks:%ld atlasTasks:%ld renderTasks:%ld",
       clock.elapsedTime(), clock.measure("", "resourceTasks"),
       clock.measure("resourceTasks", "atlasTasks"), clock.measure("atlasTasks", "renderTasks"));
  vertexMaxValueTracker.addValue(vertexAllocator.size());
  drawingMaxValueTracker.addValue(drawingAllocator.size());
  auto commandBuffer = commandEncoder->finish();
  context->globalCache()->resetUniformBuffer();
  return commandBuffer;
}

bool DrawingBuffer::empty() const {
  return resourceTasks.empty() && renderTasks.empty() && atlasTasks.empty();
}

void DrawingBuffer::reset() {
  renderTasks.clear();
  resourceTasks.clear();
  atlasTasks.clear();
  vertexAllocator.clear(vertexMaxValueTracker.getMaxValue());
  drawingAllocator.clear(drawingMaxValueTracker.getMaxValue());
  _generation++;
}

}  // namespace tgfx
