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

#include <atomic>
#include <deque>
#include <memory>
#include <vector>
#include "gpu/vulkan/VulkanAPI.h"
#include "tgfx/gpu/CommandQueue.h"
#include "vk_mem_alloc.h"

namespace tgfx {

class VulkanGPU;
class VulkanTexture;

/**
 * Vulkan command queue implementation with asynchronous fence-based submission.
 *
 * Submit flow:
 *   1. Poll completed fences and reclaim resources from finished submissions.
 *   2. If the number of in-flight submissions reaches MAX_FRAMES_IN_FLIGHT, block until the oldest
 *      fence signals (backpressure to prevent unbounded memory growth).
 *   3. Allocate a fence, submit the command buffer(s), and record the submission as in-flight.
 *
 * This design mirrors Metal's completion handler semantics: submit never blocks under normal load,
 * completedFrameTime is updated as soon as a fence is detected signaled, and ResourceCache uses
 * completedFrameTime to determine when scratch resources are safe to reuse.
 *
 * writeTexture() follows deferred semantics: pixel data is immediately snapshot into a staging
 * buffer, but the GPU copy is deferred until the next submit(). This avoids per-upload
 * synchronization overhead and allows all uploads to be batched into a single command buffer
 * submission alongside render commands.
 */
class VulkanCommandQueue : public CommandQueue {
 public:
  explicit VulkanCommandQueue(VulkanGPU* gpu);

  ~VulkanCommandQueue() override;

  std::chrono::steady_clock::time_point completedFrameTime() const override;

  void writeBuffer(std::shared_ptr<GPUBuffer> buffer, size_t bufferOffset, const void* data,
                   size_t size) override;

  void writeTexture(std::shared_ptr<Texture> texture, const Rect& rect, const void* pixels,
                    size_t rowBytes) override;

  void submit(std::shared_ptr<CommandBuffer> commandBuffer) override;

  std::shared_ptr<Semaphore> insertSemaphore() override;

  void waitSemaphore(std::shared_ptr<Semaphore> semaphore) override;

  void waitUntilCompleted() override;

 private:
  struct PendingUpload {
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    std::shared_ptr<VulkanTexture> texture;
    VkBufferImageCopy region = {};
  };

  // Represents a single vkQueueSubmit whose GPU completion has not yet been confirmed.
  struct InflightSubmission {
    VkFence fence = VK_NULL_HANDLE;
    std::chrono::steady_clock::time_point frameTime = {};
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<PendingUpload> uploads;
    // Deferred destroys: objects created during encoding that must outlive GPU execution.
    std::vector<VkFramebuffer> deferredFramebuffers;
    std::vector<VkRenderPass> deferredRenderPasses;
    std::vector<VkDescriptorPool> deferredDescriptorPools;
  };

  // Maximum number of submissions that may be in-flight simultaneously. Acts as backpressure to
  // bound memory usage when GPU is slower than CPU. Matches Metal's typical drawable count.
  static constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;

  void flushPendingUploads(VkCommandBuffer commandBuffer);
  void cleanupPendingUploads();

  // Non-blocking: polls all pending fences and reclaims resources from completed submissions.
  void pollCompletedSubmissions();

  // Blocking: waits for all in-flight submissions to complete and reclaims their resources.
  void waitAllInflightSubmissions();

  VkFence acquireFence();
  void recycleFence(VkFence fence);

  VulkanGPU* gpu = nullptr;
  std::vector<PendingUpload> pendingUploads;
  std::deque<InflightSubmission> inflightSubmissions;
  std::vector<VkFence> fencePool;
  std::atomic<int64_t> _completedFrameTime = {0};
};

}  // namespace tgfx
