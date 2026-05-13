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

#include <memory>
#include <optional>
#include <vector>
#include "gpu/vulkan/VulkanGPU.h"
#include "tgfx/gpu/CommandQueue.h"

namespace tgfx {

class VulkanSemaphore;
class VulkanTexture;

/**
 * Thin coordination layer satisfying the public CommandQueue interface. Does NOT own any GPU
 * synchronization primitives (fences, inflight tracking) — those belong to VulkanGPU.
 *
 * Holds only the data accumulated between two consecutive submit() calls:
 *   - pendingUploads: staging buffers from writeTexture(), consumed by submit().
 *   - pendingPresent: swapchain info from schedulePresent(), consumed by submit().
 *   - pendingSignal/WaitSemaphore: from insertSemaphore()/waitSemaphore(), consumed by submit().
 *
 * On submit(), this class assembles the upload/render/present command buffers and the SubmitRequest
 * struct, then delegates everything to VulkanGPU::executeSubmission() which handles fence
 * allocation, backpressure, vkQueueSubmit, inflight tracking, and vkQueuePresentKHR.
 *
 * writeTexture() uses deferred semantics: pixel data is immediately copied into a staging buffer
 * (CPU-only operation), but the GPU-side copy command is recorded at the next submit() time. This
 * allows multiple uploads to be batched into one command buffer alongside render commands.
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

  /// Schedules a swapchain image to be presented at the end of the next submit(). The layout
  /// transition (GENERAL -> PRESENT_SRC_KHR) is automatically appended to the render command batch.
  void schedulePresent(VkSwapchainKHR swapchain, uint32_t imageIndex, VkImage image,
                       VkSemaphore imageAvailableSemaphore, VkSemaphore renderFinishedSemaphore);

 private:
  void flushUploads(VkCommandBuffer commandBuffer, std::vector<VulkanGPU::PendingUpload>& uploads);
  void cleanupPendingUploads();
  void abandonSubmit(FrameSession session, std::vector<VulkanGPU::PendingUpload> uploads);

  VulkanGPU* gpu = nullptr;

  // Produced by writeTexture(), consumed by submit() which records copy commands into the
  // command buffer and then moves staging buffers into InflightSubmission for deferred cleanup.
  std::vector<VulkanGPU::PendingUpload> pendingUploads;

  // Produced by insertSemaphore()/waitSemaphore(), consumed by submit().
  std::shared_ptr<VulkanSemaphore> pendingSignalSemaphore;
  std::shared_ptr<VulkanSemaphore> pendingWaitSemaphore;

  // Produced by schedulePresent(), consumed by submit().
  struct PendingPresent {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    uint32_t imageIndex = 0;
    VkImage image = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
  };
  std::optional<PendingPresent> pendingPresent;
};

}  // namespace tgfx
