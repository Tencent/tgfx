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
#include <array>
#include "core/utils/Log.h"
#include "core/utils/RasterizedImageCacheProbe.h"
#include "core/utils/UniqueID.h"
#include "gpu/GlobalCache.h"
#include "gpu/tasks/GenerateMipmapsTask.h"
#include "gpu/tasks/ResourceTask.h"
#include "gpu/tasks/TextureUploadTask.h"
#include "inspect/InspectorMark.h"
#include "tgfx/core/Clock.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/Window.h"

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
  TASK_MARK(tgfx::inspect::OpTaskType::Flush);

  // Log a per-stage breakdown whenever encode() exceeds this threshold. On single-threaded
  // platforms (e.g. WeChat Mini Program) all resource tasks and render tasks are executed serially
  // here, so this is usually the main contributor to submit() latency.
  constexpr int64_t ENCODE_SLOW_THRESHOLD_US = 100 * 1000;  // 100ms

  constexpr size_t kTypeCount = static_cast<size_t>(ResourceTaskType::Count);
  // Display names indexed by ResourceTaskType. Must stay in lock-step with the enum order.
  static constexpr std::array<const char*, kTypeCount> kTypeNames = {
      "Unknown",   "Shape",     "Texture",   "Hairline", "VertexMesh",
      "MeshIndex", "ShapeMesh", "GPUBuffer", "Readback",
  };
  std::array<int64_t, kTypeCount> typeUs = {};
  std::array<size_t, kTypeCount> typeCount = {};

  int64_t encodeStartUs = Clock::Now();
  size_t resourceTaskCount = resourceTasks.size();
  size_t renderTaskCount = renderTasks.size();

  int64_t resourceStartUs = Clock::Now();
  {
    TASK_MARK(tgfx::inspect::OpTaskType::ResourceTask);
    for (auto& task : resourceTasks) {
      int64_t taskStartUs = Clock::Now();
      auto typeIndex = static_cast<size_t>(task->type());
      task->execute(context);
      int64_t taskEndUs = Clock::Now();
      typeUs[typeIndex] += (taskEndUs - taskStartUs);
      typeCount[typeIndex]++;
      task = nullptr;
    }
  }
  int64_t resourceEndUs = Clock::Now();

  for (auto& task : atlasTasks) {
    task->upload(context);
    task = nullptr;
  }
  int64_t atlasEndUs = Clock::Now();

  auto commandEncoder = context->gpu()->createCommandEncoder();
  {
    TASK_MARK(tgfx::inspect::OpTaskType::RenderTask);
    for (auto& task : renderTasks) {
      task->execute(commandEncoder.get());
      task = nullptr;
    }
  }
  int64_t renderEndUs = Clock::Now();

  vertexMaxValueTracker.addValue(vertexAllocator.size());
  instanceMaxValueTracker.addValue(instanceAllocator.size());
  drawingMaxValueTracker.addValue(drawingAllocator.size());
  auto commandBuffer = commandEncoder->finish();
  context->globalCache()->resetUniformBuffer();

  int64_t encodeEndUs = Clock::Now();
  int64_t totalUs = encodeEndUs - encodeStartUs;
  // Always fetch-and-reset so the counters only describe this frame's uploads. Without this, a
  // non-slow frame would leak its counters into the next frame's slow-frame report.
  auto textureProfile = TextureUploadTask::FetchProfileAndReset();
  auto mipmapProfile = GenerateMipmapsTask::FetchProfileAndReset();
  auto cacheProbe = FetchAndResetCacheProbeStats();
  if (totalUs > ENCODE_SLOW_THRESHOLD_US) {
    int64_t resourceUs = resourceEndUs - resourceStartUs;
    int64_t renderUs = renderEndUs - atlasEndUs;
    int64_t finishUs = encodeEndUs - renderEndUs;
    LOGI(
        "[DrawingBuffer] slow encode: total=%.2fms resource=%.2fms(%llu) render=%.2fms(%llu) "
        "finish=%.2fms",
        static_cast<double>(totalUs) / 1000.0, static_cast<double>(resourceUs) / 1000.0,
        static_cast<unsigned long long>(resourceTaskCount), static_cast<double>(renderUs) / 1000.0,
        static_cast<unsigned long long>(renderTaskCount), static_cast<double>(finishUs) / 1000.0);
    // Hide categories that account for less than 1% of the resource stage: they are noise and
    // clutter the log. Also skip empty categories. Guard against resourceUs<=0 to avoid division
    // edge cases.
    for (size_t i = 0; i < kTypeCount; ++i) {
      if (typeCount[i] == 0) {
        continue;
      }
      if (resourceUs > 0 && typeUs[i] * 100 < resourceUs) {
        continue;
      }
      LOGI("[DrawingBuffer]   %s=%.2fms(%llu)", kTypeNames[i],
           static_cast<double>(typeUs[i]) / 1000.0, static_cast<unsigned long long>(typeCount[i]));
    }
    if (textureProfile.count > 0) {
      LOGI(
          "[DrawingBuffer]   TextureDetail: decode=%.2fms upload=%.2fms uploaded=%.2fMB "
          "maxPixel=%dx%d",
          static_cast<double>(textureProfile.getDataUs) / 1000.0,
          static_cast<double>(textureProfile.makeTextureUs) / 1000.0,
          static_cast<double>(textureProfile.uploadedBytes) / (1024.0 * 1024.0),
          textureProfile.maxPixelWidth, textureProfile.maxPixelHeight);
    }
    if (mipmapProfile.count > 0) {
      LOGI("[DrawingBuffer]   GenMipmap: time=%.2fms count=%u",
           static_cast<double>(mipmapProfile.totalUs) / 1000.0, mipmapProfile.count);
    }
    if (cacheProbe.total() > 0) {
      LOGI("[DrawingBuffer]   CacheMiss: first=%u newScale=%u rebuild=%u", cacheProbe.first,
           cacheProbe.newScale, cacheProbe.rebuild);
    }
    // Print current GPU resource cache footprint so we can tell eviction caused by capacity
    // versus by age: if rebuild>0 but cacheBytes is nowhere near the configured limit, the
    // eviction cannot be a simple totalBytes>maxBytes trigger and some other mechanism is at
    // play (e.g. tile-surface churn, scratch-resource downgrade).
    LOGI("[DrawingBuffer]   CacheBytes: %.2fMB",
         static_cast<double>(context->memoryUsage()) / (1024.0 * 1024.0));
  }

  return commandBuffer;
}

void DrawingBuffer::presentWindows(Context* context) {
  bool presented = false;
  for (auto& pendingWindow : windows) {
    if (auto window = pendingWindow.lock()) {
      window->onPresent(context);
      presented = true;
    }
  }
  if (presented) {
    FRAME_MARK;
  }
}

bool DrawingBuffer::empty() const {
  return resourceTasks.empty() && renderTasks.empty() && atlasTasks.empty();
}

void DrawingBuffer::reset() {
  renderTasks.clear();
  resourceTasks.clear();
  atlasTasks.clear();
  windows.clear();
  vertexAllocator.clear(vertexMaxValueTracker.getMaxValue());
  instanceAllocator.clear(instanceMaxValueTracker.getMaxValue());
  drawingAllocator.clear(drawingMaxValueTracker.getMaxValue());
  _generation++;
}

}  // namespace tgfx
