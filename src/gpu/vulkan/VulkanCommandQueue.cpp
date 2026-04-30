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
#include "vk_mem_alloc.h"

namespace tgfx {

VulkanCommandQueue::VulkanCommandQueue(VulkanGPU* gpu) : gpu(gpu) {
}

VulkanCommandQueue::~VulkanCommandQueue() {
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
  auto device = gpu->device();

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

  auto srcRowBytes = rowBytes > 0 ? rowBytes : tightRowBytes;
  auto dst = static_cast<uint8_t*>(stagingAllocInfo.pMappedData);
  auto src = static_cast<const uint8_t*>(pixels);
  for (uint32_t row = 0; row < height; row++) {
    memcpy(dst + row * tightRowBytes, src + row * srcRowBytes, tightRowBytes);
  }

  VkCommandPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = gpu->graphicsQueueIndex();

  VkCommandPool cmdPool = VK_NULL_HANDLE;
  vkCreateCommandPool(device, &poolInfo, nullptr, &cmdPool);

  VkCommandBufferAllocateInfo cmdAllocInfo = {};
  cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdAllocInfo.commandPool = cmdPool;
  cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdAllocInfo.commandBufferCount = 1;

  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &beginInfo);

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = vulkanTexture->currentLayout();
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = vulkanTexture->vulkanImage();
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);

  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset = {static_cast<int32_t>(rect.x()), static_cast<int32_t>(rect.y()), 0};
  region.imageExtent = {width, height, 1};

  vkCmdCopyBufferToImage(cmd, stagingBuffer, vulkanTexture->vulkanImage(),
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  // Ensure the transfer write is visible to subsequent fragment shader reads and color attachment
  // operations. This runs in an isolated command buffer with vkQueueWaitIdle, so the texture is
  // ready for sampling by the time the next command buffer is submitted.
  vkCmdPipelineBarrier(
      cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
      nullptr, 0, nullptr, 1, &barrier);

  vkEndCommandBuffer(cmd);

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;

  vkQueueSubmit(gpu->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(gpu->graphicsQueue());

  vulkanTexture->setCurrentLayout(VK_IMAGE_LAYOUT_GENERAL);

  vkDestroyCommandPool(device, cmdPool, nullptr);
  vmaDestroyBuffer(gpu->allocator(), stagingBuffer, stagingAlloc);
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

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;

  vkQueueSubmit(gpu->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(gpu->graphicsQueue());

  auto pool = vulkanCmdBuffer->vulkanCommandPool();
  if (pool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(gpu->device(), pool, nullptr);
  }
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
  auto device = gpu->device();
  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
  }
}

}  // namespace tgfx
