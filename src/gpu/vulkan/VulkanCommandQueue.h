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
#include <vector>
#include "gpu/vulkan/VulkanAPI.h"
#include "tgfx/gpu/CommandQueue.h"
#include "vk_mem_alloc.h"

namespace tgfx {

class VulkanGPU;
class VulkanTexture;

/**
 * Vulkan command queue implementation.
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

  void flushPendingUploads(VkCommandBuffer commandBuffer);
  void cleanupPendingUploads();

  VulkanGPU* gpu = nullptr;
  std::vector<PendingUpload> pendingUploads;
};

}  // namespace tgfx
