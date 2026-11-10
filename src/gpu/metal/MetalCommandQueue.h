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

#include <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <atomic>
#include <deque>
#include <vector>
#include "tgfx/gpu/CommandQueue.h"

namespace tgfx {

class MetalGPU;
class MetalSemaphore;

/**
 * Metal command queue implementation.
 */
class MetalCommandQueue : public CommandQueue {
 public:
  explicit MetalCommandQueue(MetalGPU* gpu);
  ~MetalCommandQueue() override;

  /**
   * Returns the Metal command queue.
   */
  id<MTLCommandQueue> metalCommandQueue() const {
    return commandQueue;
  }

  std::chrono::steady_clock::time_point completedFrameTime() const override {
    auto ticks = _completedFrameTime.load(std::memory_order_acquire);
    return std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(ticks));
  }

  void submit(std::shared_ptr<CommandBuffer> commandBuffer) override;

  void writeBuffer(std::shared_ptr<GPUBuffer> buffer, size_t bufferOffset, const void* data,
                   size_t dataSize) override;

  void writeTexture(std::shared_ptr<Texture> texture, const Rect& rect, const void* pixels,
                    size_t rowBytes) override;

  /**
   * Commits pending texture uploads before encoding commands that consume them, without waiting
   * for GPU completion.
   */
  void flushTextureUploads();

  std::shared_ptr<Semaphore> insertSemaphore() override;

  void waitSemaphore(std::shared_ptr<Semaphore> semaphore) override;

  void waitUntilCompleted() override;

  /**
   * Encodes the pending wait semaphore into the given command buffer and clears it.
   */
  void encodePendingWait(id<MTLCommandBuffer> commandBuffer);

  /**
   * Schedules a drawable to be presented with the next committed command buffer.
   */
  void schedulePresent(id<CAMetalDrawable> drawable);

 private:
  static constexpr size_t MAX_UPLOAD_BATCH_BYTES = 16 * 1024 * 1024;
  static constexpr size_t MAX_UPLOAD_BATCH_COPIES = 128;
  static constexpr size_t MAX_IN_FLIGHT_UPLOAD_BYTES = 64 * 1024 * 1024;

  struct TextureUploadSubmission {
    id<MTLCommandBuffer> commandBuffer = nil;
    size_t bytes = 0;
  };

  bool ensureTextureUploadEncoder();
  void reserveTextureUploadBytes(size_t bytes);
  void collectCompletedTextureUploads();
  void waitForTextureUploads();

  MetalGPU* gpu = nullptr;
  id<MTLCommandQueue> commandQueue = nil;
  id<MTLCommandBuffer> lastSubmittedCommandBuffer = nil;
  id<MTLCommandBuffer> pendingUploadCommandBuffer = nil;
  id<MTLBlitCommandEncoder> pendingUploadEncoder = nil;
  size_t pendingUploadBytes = 0;
  size_t pendingUploadCopies = 0;
  size_t inFlightUploadBytes = 0;
  std::deque<TextureUploadSubmission> uploadSubmissions = {};
  std::shared_ptr<MetalSemaphore> pendingSignalSemaphore = nullptr;
  std::shared_ptr<MetalSemaphore> pendingWaitSemaphore = nullptr;
  std::vector<id<CAMetalDrawable>> pendingDrawables = {};
  std::atomic<int64_t> _completedFrameTime = {0};
};

}  // namespace tgfx