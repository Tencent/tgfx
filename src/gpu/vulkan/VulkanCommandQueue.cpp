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
#include <unordered_set>
#include "VulkanBuffer.h"
#include "VulkanCommandBuffer.h"
#include "VulkanSemaphore.h"
#include "VulkanTexture.h"
#include "VulkanUtil.h"
#include "core/utils/Log.h"

namespace tgfx {

VulkanCommandQueue::VulkanCommandQueue(VulkanGPU* gpu) : gpu(gpu) {
}

VulkanCommandQueue::~VulkanCommandQueue() {
  cleanupPendingUploads();
}

std::chrono::steady_clock::time_point VulkanCommandQueue::completedFrameTime() const {
  return gpu->lastFenceSignalTime();
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

  VkBufferCreateInfo bufferInfo = {};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = stagingSize;
  bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocInfo = {};
  allocInfo.flags =
      VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
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

  auto srcRowBytes = rowBytes > 0 ? rowBytes : tightRowBytes;
  auto dst = static_cast<uint8_t*>(stagingAllocInfo.pMappedData);
  auto src = static_cast<const uint8_t*>(pixels);
  for (uint32_t row = 0; row < height; row++) {
    memcpy(dst + row * tightRowBytes, src + row * srcRowBytes, tightRowBytes);
  }

  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset = {static_cast<int32_t>(rect.x()), static_cast<int32_t>(rect.y()), 0};
  region.imageExtent = {width, height, 1};

  auto originalLayout = vulkanTexture->currentLayout();
  pendingUploads.push_back({stagingBuffer, stagingAlloc, vulkanTexture, region, originalLayout});
  vulkanTexture->setCurrentLayout(VK_IMAGE_LAYOUT_GENERAL);
}

void VulkanCommandQueue::flushPendingUploads(VkCommandBuffer commandBuffer) {
  if (pendingUploads.empty()) {
    return;
  }

  std::vector<VkImageMemoryBarrier> preCopyBarriers;
  std::unordered_set<VkImage> transitionedImages;
  for (auto& upload : pendingUploads) {
    auto image = upload.texture->vulkanImage();
    if (transitionedImages.count(image)) {
      continue;
    }
    transitionedImages.insert(image);
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = upload.originalLayout;
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

  for (auto& upload : pendingUploads) {
    vkCmdCopyBufferToImage(commandBuffer, upload.stagingBuffer, upload.texture->vulkanImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &upload.region);
  }

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

  std::vector<VkCommandBuffer> cmdBuffers;
  if (uploadCmd != VK_NULL_HANDLE) {
    cmdBuffers.push_back(uploadCmd);
  }
  cmdBuffers.push_back(cmd);

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

  VulkanGPU::SubmitRequest request = {};
  request.session = std::move(vulkanCmdBuffer->frameSession());
  request.uploads = std::move(pendingUploads);
  request.commandBuffers = std::move(cmdBuffers);
  request.signalSemaphore = std::move(pendingSignalSemaphore);
  request.waitSemaphore = std::move(pendingWaitSemaphore);
  request.frameTime = _frameTime;
  if (pendingPresent.has_value()) {
    request.presentSwapchain = pendingPresent->swapchain;
    request.presentImageIndex = pendingPresent->imageIndex;
    request.imageAvailableSemaphore = pendingPresent->imageAvailableSemaphore;
    request.renderFinishedSemaphore = pendingPresent->renderFinishedSemaphore;
  }

  pendingUploads.clear();
  pendingSignalSemaphore = nullptr;
  pendingWaitSemaphore = nullptr;
  pendingPresent.reset();

  gpu->executeSubmission(std::move(request));
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
                                         VkImage image, VkSemaphore imageAvailableSemaphore,
                                         VkSemaphore renderFinishedSemaphore) {
  pendingPresent = {swapchain, imageIndex, image, imageAvailableSemaphore,
                    renderFinishedSemaphore};
}

void VulkanCommandQueue::waitUntilCompleted() {
  if (gpu == nullptr) {
    return;
  }
  if (!pendingUploads.empty()) {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = gpu->graphicsQueueIndex();
    if (vkCreateCommandPool(gpu->device(), &poolInfo, nullptr, &pool) != VK_SUCCESS) {
      LOGE("VulkanCommandQueue::waitUntilCompleted: failed to create command pool for flush.");
      cleanupPendingUploads();
      gpu->waitAllInflightSubmissions();
      return;
    }
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(gpu->device(), &allocInfo, &cmd) != VK_SUCCESS) {
      LOGE("VulkanCommandQueue::waitUntilCompleted: failed to allocate command buffer for flush.");
      vkDestroyCommandPool(gpu->device(), pool, nullptr);
      cleanupPendingUploads();
      gpu->waitAllInflightSubmissions();
      return;
    }
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    flushPendingUploads(cmd);
    vkEndCommandBuffer(cmd);

    VulkanGPU::SubmitRequest request = {};
    request.session.commandPool = pool;
    request.commandBuffers = {cmd};
    request.uploads = std::move(pendingUploads);
    request.frameTime = _frameTime;
    pendingUploads.clear();
    gpu->executeSubmission(std::move(request));
  }
  gpu->waitAllInflightSubmissions();
}

}  // namespace tgfx
