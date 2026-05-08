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

void VulkanCommandQueue::reclaimSubmission(InflightSubmission& submission) {
  for (auto& upload : submission.uploads) {
    vmaDestroyBuffer(gpu->allocator(), upload.stagingBuffer, upload.stagingAlloc);
  }
  if (submission.commandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(gpu->device(), submission.commandPool, nullptr);
  }
  for (auto fb : submission.deferredFramebuffers) {
    vkDestroyFramebuffer(gpu->device(), fb, nullptr);
  }
  for (auto rp : submission.deferredRenderPasses) {
    vkDestroyRenderPass(gpu->device(), rp, nullptr);
  }
  for (auto pool : submission.deferredDescriptorPools) {
    gpu->releaseDescriptorPool(pool);
  }
  submission.retainedResources.clear();
  recycleFence(submission.fence);
}

void VulkanCommandQueue::pollCompletedSubmissions() {
  while (!inflightSubmissions.empty()) {
    auto& oldest = inflightSubmissions.front();
    if (vkGetFenceStatus(gpu->device(), oldest.fence) != VK_SUCCESS) {
      break;
    }
    auto ticks = oldest.frameTime.time_since_epoch().count();
    _completedFrameTime.store(ticks, std::memory_order_release);
    reclaimSubmission(oldest);
    inflightSubmissions.pop_front();
  }
  gpu->processUnreferencedResources();
}

void VulkanCommandQueue::waitAllInflightSubmissions() {
  for (auto& submission : inflightSubmissions) {
    vkWaitForFences(gpu->device(), 1, &submission.fence, VK_TRUE, UINT64_MAX);
    auto ticks = submission.frameTime.time_since_epoch().count();
    _completedFrameTime.store(ticks, std::memory_order_release);
    reclaimSubmission(submission);
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
  allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

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

  // Deduplicate images: each unique image gets exactly one pre-copy and one post-copy barrier.
  std::vector<VkImageMemoryBarrier> preCopyBarriers;
  std::vector<VkImage> transitionedImages;
  for (auto& upload : pendingUploads) {
    auto image = upload.texture->vulkanImage();
    bool alreadyTransitioned = false;
    for (auto img : transitionedImages) {
      if (img == image) {
        alreadyTransitioned = true;
        break;
      }
    }
    if (alreadyTransitioned) {
      continue;
    }
    transitionedImages.push_back(image);
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    // Use tracked layout to preserve existing contents outside the upload region.
    // UNDEFINED would discard the entire image, corrupting previously uploaded sub-rects.
    barrier.oldLayout = upload.texture->currentLayout();
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1};
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

  // Post-copy: transition each unique image from TRANSFER_DST to GENERAL.
  std::vector<VkImageMemoryBarrier> postCopyBarriers;
  postCopyBarriers.reserve(transitionedImages.size());
  for (auto image : transitionedImages) {
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    postCopyBarriers.push_back(barrier);
  }
  for (auto& upload : pendingUploads) {
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
    } else {
      LOGE("VulkanCommandQueue::submit: failed to allocate upload command buffer (result=%d).",
           static_cast<int>(result));
      return;
    }
  }

  // Submit upload + render command buffers with a fence for async completion tracking.
  std::vector<VkCommandBuffer> cmdBuffers;
  if (uploadCmd != VK_NULL_HANDLE) {
    cmdBuffers.push_back(uploadCmd);
  }
  cmdBuffers.push_back(cmd);

  // If a present is scheduled, append the GENERAL → PRESENT_SRC layout transition to this batch.
  VkCommandBuffer presentCmd = VK_NULL_HANDLE;
  if (pendingPresent.has_value() && renderPool != VK_NULL_HANDLE) {
    VkCommandBufferAllocateInfo presentAllocInfo = {};
    presentAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    presentAllocInfo.commandPool = renderPool;
    presentAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    presentAllocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(gpu->device(), &presentAllocInfo, &presentCmd) == VK_SUCCESS) {
      VkCommandBufferBeginInfo presentBeginInfo = {};
      presentBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      presentBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(presentCmd, &presentBeginInfo);

      VkImageMemoryBarrier barrier = {};
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
      barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = pendingPresent->image;
      barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      barrier.dstAccessMask = 0;
      vkCmdPipelineBarrier(presentCmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                           &barrier);
      vkEndCommandBuffer(presentCmd);
      cmdBuffers.push_back(presentCmd);
    }
  }

  VkFence fence = acquireFence();

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = static_cast<uint32_t>(cmdBuffers.size());
  submitInfo.pCommandBuffers = cmdBuffers.data();

  // Chain pending timeline semaphore operations into this submission.
  VkTimelineSemaphoreSubmitInfo timelineInfo = {};
  timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
  VkSemaphore waitSem = VK_NULL_HANDLE;
  VkSemaphore signalSem = VK_NULL_HANDLE;
  uint64_t waitValue = 0;
  uint64_t signalValue = 0;
  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

  if (pendingWaitSemaphore) {
    waitSem = pendingWaitSemaphore->vulkanSemaphore();
    waitValue = pendingWaitSemaphore->signalValue();
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &waitSem;
    submitInfo.pWaitDstStageMask = &waitStage;
    timelineInfo.waitSemaphoreValueCount = 1;
    timelineInfo.pWaitSemaphoreValues = &waitValue;
  }
  if (pendingSignalSemaphore) {
    signalValue = pendingSignalSemaphore->nextSignalValue();
    signalSem = pendingSignalSemaphore->vulkanSemaphore();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSem;
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &signalValue;
  }
  if (pendingWaitSemaphore || pendingSignalSemaphore) {
    submitInfo.pNext = &timelineInfo;
  }

  auto submitResult = vkQueueSubmit(gpu->graphicsQueue(), 1, &submitInfo, fence);

  // Clear pending semaphores regardless of submit success — they were either consumed or the
  // submission failed (in which case retrying with stale semaphore state would be wrong).
  pendingWaitSemaphore = nullptr;
  pendingSignalSemaphore = nullptr;

  if (submitResult != VK_SUCCESS) {
    LOGE("VulkanCommandQueue::submit: vkQueueSubmit failed (result=%d).",
         static_cast<int>(submitResult));
    // Submit failed — fence was never signaled. Pack all resources into a temporary
    // InflightSubmission and reclaim immediately to avoid leaks or future deadlocks.
    InflightSubmission failed = {};
    failed.fence = fence;
    failed.commandPool = vulkanCmdBuffer->vulkanCommandPool();
    failed.uploads = std::move(pendingUploads);
    failed.deferredFramebuffers = std::move(vulkanCmdBuffer->deferredFramebuffers());
    failed.deferredRenderPasses = std::move(vulkanCmdBuffer->deferredRenderPasses());
    if (vulkanCmdBuffer->descriptorPool() != VK_NULL_HANDLE) {
      failed.deferredDescriptorPools.push_back(vulkanCmdBuffer->descriptorPool());
    }
    failed.retainedResources = std::move(vulkanCmdBuffer->retainedResources());
    reclaimSubmission(failed);
    pendingUploads.clear();
    return;
  }

  // Transfer ownership of per-frame resources to the inflight record. These will be reclaimed
  // once the fence signals (either via pollCompletedSubmissions or waitAllInflightSubmissions).
  InflightSubmission submission = {};
  submission.fence = fence;
  submission.frameTime = _frameTime;
  submission.commandPool = vulkanCmdBuffer->vulkanCommandPool();
  submission.uploads = std::move(pendingUploads);
  submission.deferredFramebuffers = std::move(vulkanCmdBuffer->deferredFramebuffers());
  submission.deferredRenderPasses = std::move(vulkanCmdBuffer->deferredRenderPasses());
  if (vulkanCmdBuffer->descriptorPool() != VK_NULL_HANDLE) {
    submission.deferredDescriptorPools.push_back(vulkanCmdBuffer->descriptorPool());
  }
  submission.retainedResources = std::move(vulkanCmdBuffer->retainedResources());
  inflightSubmissions.push_back(std::move(submission));
  pendingUploads.clear();

  // Execute pending present after the submit. Same-queue ordering guarantees the layout
  // transition (included in this submit batch) completes before the presentation engine reads.
  if (pendingPresent.has_value()) {
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &pendingPresent->swapchain;
    presentInfo.pImageIndices = &pendingPresent->imageIndex;
    vkQueuePresentKHR(gpu->graphicsQueue(), &presentInfo);
    pendingPresent.reset();
  }
}

std::shared_ptr<Semaphore> VulkanCommandQueue::insertSemaphore() {
  auto semaphore = VulkanSemaphore::Make(gpu);
  if (semaphore == nullptr) {
    return nullptr;
  }
  pendingSignalSemaphore = semaphore;
  return semaphore;
}

void VulkanCommandQueue::waitSemaphore(std::shared_ptr<Semaphore> semaphore) {
  if (semaphore == nullptr) {
    return;
  }
  pendingWaitSemaphore = std::static_pointer_cast<VulkanSemaphore>(semaphore);
}

void VulkanCommandQueue::schedulePresent(VkSwapchainKHR swapchain, uint32_t imageIndex,
                                         VkImage image) {
  pendingPresent = {swapchain, imageIndex, image};
}

void VulkanCommandQueue::waitUntilCompleted() {
  if (gpu == nullptr) {
    return;
  }
  waitAllInflightSubmissions();
  // Pending uploads are intentionally preserved: they will be executed on the next submit().
  // This matches Metal/GL semantics where waitUntilCompleted only waits for already-submitted work.
}

}  // namespace tgfx
