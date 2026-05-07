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

#include "VulkanCommandQueue.h"
#include "VulkanBuffer.h"
#include "VulkanCommandBuffer.h"
#include "VulkanGPU.h"
#include "VulkanSemaphore.h"
#include "VulkanTexture.h"
#include "VulkanUtil.h"
#include "core/utils/Log.h"

namespace tgfx {

VulkanCommandQueue::VulkanCommandQueue(VulkanGPU* gpu) : gpu(gpu) {
}

VulkanCommandQueue::~VulkanCommandQueue() {
  waitAllInflightSubmissions();
  cleanupPendingUploads();
  for (auto fence : fencePool) {
    vkDestroyFence(gpu->device(), fence, nullptr);
  }
}

std::chrono::steady_clock::time_point VulkanCommandQueue::completedFrameTime() const {
  auto ticks = _completedFrameTime.load(std::memory_order_acquire);
  return std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(ticks));
}

VkFence VulkanCommandQueue::acquireFence() {
  if (!fencePool.empty()) {
    auto fence = fencePool.back();
    fencePool.pop_back();
    vkResetFences(gpu->device(), 1, &fence);
    return fence;
  }
  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence = VK_NULL_HANDLE;
  vkCreateFence(gpu->device(), &fenceInfo, nullptr, &fence);
  return fence;
}

void VulkanCommandQueue::recycleFence(VkFence fence) {
  fencePool.push_back(fence);
}

void VulkanCommandQueue::pollCompletedSubmissions() {
  bool anyCompleted = false;
  while (!inflightSubmissions.empty()) {
    auto& oldest = inflightSubmissions.front();
    if (vkGetFenceStatus(gpu->device(), oldest.fence) != VK_SUCCESS) {
      break;
    }
    auto ticks = oldest.frameTime.time_since_epoch().count();
    _completedFrameTime.store(ticks, std::memory_order_release);
    for (auto& upload : oldest.uploads) {
      vmaDestroyBuffer(gpu->allocator(), upload.stagingBuffer, upload.stagingAlloc);
    }
    if (oldest.commandPool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(gpu->device(), oldest.commandPool, nullptr);
    }
    recycleFence(oldest.fence);
    inflightSubmissions.pop_front();
    anyCompleted = true;
  }
  // Release VulkanResources (encoders, framebuffers, render passes, descriptor pools) that have
  // become unreferenced. This is safe only after fence confirmation because these resources may
  // still be in use by the GPU until their associated submission completes.
  if (anyCompleted) {
    gpu->processUnreferencedResources();
  }
}

void VulkanCommandQueue::waitAllInflightSubmissions() {
  for (auto& submission : inflightSubmissions) {
    vkWaitForFences(gpu->device(), 1, &submission.fence, VK_TRUE, UINT64_MAX);
    auto ticks = submission.frameTime.time_since_epoch().count();
    _completedFrameTime.store(ticks, std::memory_order_release);
    for (auto& upload : submission.uploads) {
      vmaDestroyBuffer(gpu->allocator(), upload.stagingBuffer, upload.stagingAlloc);
    }
    if (submission.commandPool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(gpu->device(), submission.commandPool, nullptr);
    }
    recycleFence(submission.fence);
  }
  inflightSubmissions.clear();
  gpu->processUnreferencedResources();
}

void VulkanCommandQueue::writeBuffer(std::shared_ptr<GPUBuffer> buffer, size_t bufferOffset,
                                     const void* data, size_t size) {
  if (!buffer || !data || size == 0) {
    return;
  }
  auto vulkanBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);
  auto mapped = vulkanBuffer->map(bufferOffset, size);
  if (mapped) {
    memcpy(mapped, data, size);
    vulkanBuffer->unmap();
  }
}

void VulkanCommandQueue::writeTexture(std::shared_ptr<Texture> texture, const Rect& rect,
                                      const void* pixels, size_t rowBytes) {
  if (!texture || !pixels) {
    return;
  }
  auto vulkanTexture = std::static_pointer_cast<VulkanTexture>(texture);

  auto width = static_cast<uint32_t>(rect.width());
  auto height = static_cast<uint32_t>(rect.height());
  auto bytesPerPixel = VkFormatBytesPerPixel(vulkanTexture->vulkanFormat());
  size_t tightRowBytes = width * bytesPerPixel;
  size_t stagingSize = tightRowBytes * height;

  // Allocate a staging buffer and snapshot the pixel data immediately. The actual GPU copy is
  // deferred until submit(), following the WebGPU-style "snapshot now, execute later" semantics.
  VkBufferCreateInfo bufferInfo = {};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = stagingSize;
  bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocInfo = {};
  allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
  allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VmaAllocation stagingAlloc = VK_NULL_HANDLE;
  VmaAllocationInfo stagingAllocInfo = {};
  auto result = vmaCreateBuffer(gpu->allocator(), &bufferInfo, &allocInfo, &stagingBuffer,
                                &stagingAlloc, &stagingAllocInfo);
  if (result != VK_SUCCESS) {
    LOGE("VulkanCommandQueue::writeTexture: staging buffer creation failed.");
    return;
  }

  // Snapshot pixel data into the staging buffer (CPU-only operation).
  auto srcRowBytes = rowBytes > 0 ? rowBytes : tightRowBytes;
  auto dst = static_cast<uint8_t*>(stagingAllocInfo.pMappedData);
  auto src = static_cast<const uint8_t*>(pixels);
  for (uint32_t row = 0; row < height; row++) {
    memcpy(dst + row * tightRowBytes, src + row * srcRowBytes, tightRowBytes);
  }

  // Record the pending upload. The GPU copy command will be recorded into the command buffer
  // at submit() time, batched with all other pending uploads using barrier coalescing.
  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset = {static_cast<int32_t>(rect.x()), static_cast<int32_t>(rect.y()), 0};
  region.imageExtent = {width, height, 1};

  pendingUploads.push_back({stagingBuffer, stagingAlloc, vulkanTexture, region});
}

void VulkanCommandQueue::flushPendingUploads(VkCommandBuffer commandBuffer) {
  if (pendingUploads.empty()) {
    return;
  }

  // Barrier coalescing: transition all target images to TRANSFER_DST in a single barrier call.
  // This minimizes GPU pipeline stalls compared to per-upload individual barriers.
  std::vector<VkImageMemoryBarrier> preCopyBarriers;
  preCopyBarriers.reserve(pendingUploads.size());
  for (auto& upload : pendingUploads) {
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = upload.texture->currentLayout();
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = upload.texture->vulkanImage();
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    preCopyBarriers.push_back(barrier);
  }
  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                       static_cast<uint32_t>(preCopyBarriers.size()), preCopyBarriers.data());

  // Record all buffer-to-image copy commands.
  for (auto& upload : pendingUploads) {
    vkCmdCopyBufferToImage(commandBuffer, upload.stagingBuffer, upload.texture->vulkanImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &upload.region);
  }

  // Barrier coalescing: transition all images from TRANSFER_DST to GENERAL in a single call.
  // This ensures the copy results are visible to subsequent fragment shader reads.
  std::vector<VkImageMemoryBarrier> postCopyBarriers;
  postCopyBarriers.reserve(pendingUploads.size());
  for (auto& upload : pendingUploads) {
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = upload.texture->vulkanImage();
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    postCopyBarriers.push_back(barrier);
    upload.texture->setCurrentLayout(VK_IMAGE_LAYOUT_GENERAL);
  }
  vkCmdPipelineBarrier(
      commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
      nullptr, 0, nullptr, static_cast<uint32_t>(postCopyBarriers.size()), postCopyBarriers.data());
}

void VulkanCommandQueue::cleanupPendingUploads() {
  for (auto& upload : pendingUploads) {
    vmaDestroyBuffer(gpu->allocator(), upload.stagingBuffer, upload.stagingAlloc);
  }
  pendingUploads.clear();
}

void VulkanCommandQueue::submit(std::shared_ptr<CommandBuffer> commandBuffer) {
  if (!commandBuffer) {
    return;
  }
  auto vulkanCmdBuffer = std::static_pointer_cast<VulkanCommandBuffer>(commandBuffer);
  auto cmd = vulkanCmdBuffer->vulkanCommandBuffer();
  if (cmd == VK_NULL_HANDLE) {
    return;
  }

  // Reclaim resources from any submissions that the GPU has already completed (non-blocking).
  pollCompletedSubmissions();

  // Backpressure: if we have reached the maximum allowed in-flight submissions, block until the
  // oldest one completes. This bounds memory usage when GPU is slower than CPU.
  if (inflightSubmissions.size() >= MAX_FRAMES_IN_FLIGHT) {
    auto& oldest = inflightSubmissions.front();
    vkWaitForFences(gpu->device(), 1, &oldest.fence, VK_TRUE, UINT64_MAX);
    pollCompletedSubmissions();
  }

  // Allocate the upload command buffer from the render command pool so that both are destroyed
  // together after the fence signals. This avoids the race condition of resetting a shared
  // transfer pool while its command buffers are still executing on the GPU.
  auto renderPool = vulkanCmdBuffer->vulkanCommandPool();
  VkCommandBuffer uploadCmd = VK_NULL_HANDLE;
  if (!pendingUploads.empty() && renderPool != VK_NULL_HANDLE) {
    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = renderPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    auto result = vkAllocateCommandBuffers(gpu->device(), &cmdAllocInfo, &uploadCmd);
    if (result == VK_SUCCESS) {
      VkCommandBufferBeginInfo beginInfo = {};
      beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(uploadCmd, &beginInfo);
      flushPendingUploads(uploadCmd);
      vkEndCommandBuffer(uploadCmd);
    }
  }

  // Submit upload + render command buffers with a fence for async completion tracking.
  std::vector<VkCommandBuffer> cmdBuffers;
  if (uploadCmd != VK_NULL_HANDLE) {
    cmdBuffers.push_back(uploadCmd);
  }
  cmdBuffers.push_back(cmd);

  VkFence fence = acquireFence();

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = static_cast<uint32_t>(cmdBuffers.size());
  submitInfo.pCommandBuffers = cmdBuffers.data();

  vkQueueSubmit(gpu->graphicsQueue(), 1, &submitInfo, fence);

  // Transfer ownership of per-frame resources to the inflight record. These will be reclaimed
  // once the fence signals (either via pollCompletedSubmissions or waitAllInflightSubmissions).
  inflightSubmissions.push_back({fence, _frameTime, renderPool, std::move(pendingUploads)});
  pendingUploads.clear();
}

std::shared_ptr<Semaphore> VulkanCommandQueue::insertSemaphore() {
  return VulkanSemaphore::Make(gpu);
}

void VulkanCommandQueue::waitSemaphore(std::shared_ptr<Semaphore>) {
  // TODO: Implement GPU wait on semaphore.
}

void VulkanCommandQueue::waitUntilCompleted() {
  if (gpu == nullptr) {
    return;
  }
  waitAllInflightSubmissions();
  cleanupPendingUploads();
}

}  // namespace tgfx
