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

#include "VulkanCommandEncoder.h"
#include "VulkanBuffer.h"
#include "VulkanCommandBuffer.h"
#include "VulkanGPU.h"
#include "VulkanRenderPass.h"
#include "VulkanTexture.h"
#include "VulkanUtil.h"
#include "core/utils/Log.h"

namespace tgfx {

static void TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
                                  VkImageLayout newLayout) {
  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

std::shared_ptr<VulkanCommandEncoder> VulkanCommandEncoder::Make(VulkanGPU* gpu) {
  if (!gpu) {
    return nullptr;
  }

  VkCommandPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = gpu->graphicsQueueIndex();

  VkCommandPool pool = VK_NULL_HANDLE;
  auto result = vkCreateCommandPool(gpu->device(), &poolInfo, nullptr, &pool);
  if (result != VK_SUCCESS) {
    LOGE("VulkanCommandEncoder: vkCreateCommandPool failed.");
    return nullptr;
  }

  VkCommandBufferAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = pool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
  result = vkAllocateCommandBuffers(gpu->device(), &allocInfo, &cmdBuffer);
  if (result != VK_SUCCESS) {
    LOGE("VulkanCommandEncoder: vkAllocateCommandBuffers failed.");
    vkDestroyCommandPool(gpu->device(), pool, nullptr);
    return nullptr;
  }

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmdBuffer, &beginInfo);

  return gpu->makeResource<VulkanCommandEncoder>(gpu, cmdBuffer, pool);
}

VulkanCommandEncoder::VulkanCommandEncoder(VulkanGPU* gpu, VkCommandBuffer commandBuffer,
                                           VkCommandPool commandPool)
    : _gpu(gpu), commandBuffer(commandBuffer), commandPool(commandPool) {
}

void VulkanCommandEncoder::onRelease(VulkanGPU* gpu) {
  if (commandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(gpu->device(), commandPool, nullptr);
    commandPool = VK_NULL_HANDLE;
    commandBuffer = VK_NULL_HANDLE;
  }
}

GPU* VulkanCommandEncoder::gpu() const {
  return _gpu;
}

std::shared_ptr<RenderPass> VulkanCommandEncoder::onBeginRenderPass(
    const RenderPassDescriptor& descriptor) {
  return VulkanRenderPass::Make(this, descriptor);
}

void VulkanCommandEncoder::copyTextureToTexture(std::shared_ptr<Texture> srcTexture,
                                                const Rect& srcRect,
                                                std::shared_ptr<Texture> dstTexture,
                                                const Point& dstOffset) {
  if (!srcTexture || !dstTexture) {
    return;
  }
  auto vulkanSrc = std::static_pointer_cast<VulkanTexture>(srcTexture);
  auto vulkanDst = std::static_pointer_cast<VulkanTexture>(dstTexture);

  TransitionImageLayout(commandBuffer, vulkanSrc->vulkanImage(),
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  TransitionImageLayout(commandBuffer, vulkanDst->vulkanImage(),
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  VkImageCopy region = {};
  region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.srcOffset = {static_cast<int32_t>(srcRect.x()), static_cast<int32_t>(srcRect.y()), 0};
  region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.dstOffset = {static_cast<int32_t>(dstOffset.x), static_cast<int32_t>(dstOffset.y), 0};
  region.extent = {static_cast<uint32_t>(srcRect.width()), static_cast<uint32_t>(srcRect.height()),
                   1};

  vkCmdCopyImage(commandBuffer, vulkanSrc->vulkanImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 vulkanDst->vulkanImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void VulkanCommandEncoder::copyTextureToBuffer(std::shared_ptr<Texture> srcTexture,
                                               const Rect& srcRect,
                                               std::shared_ptr<GPUBuffer> dstBuffer,
                                               size_t dstOffset, size_t dstRowBytes) {
  if (!srcTexture || !dstBuffer) {
    return;
  }
  auto vulkanSrc = std::static_pointer_cast<VulkanTexture>(srcTexture);
  auto vulkanDst = std::static_pointer_cast<VulkanBuffer>(dstBuffer);

  TransitionImageLayout(commandBuffer, vulkanSrc->vulkanImage(),
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

  auto bytesPerPixel = VkFormatBytesPerPixel(vulkanSrc->vulkanFormat());
  uint32_t rowBytes = dstRowBytes > 0 ? static_cast<uint32_t>(dstRowBytes)
                                      : static_cast<uint32_t>(srcRect.width()) * bytesPerPixel;

  VkBufferImageCopy region = {};
  region.bufferOffset = dstOffset;
  region.bufferRowLength = rowBytes / bytesPerPixel;
  region.bufferImageHeight = static_cast<uint32_t>(srcRect.height());
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset = {static_cast<int32_t>(srcRect.x()), static_cast<int32_t>(srcRect.y()), 0};
  region.imageExtent = {static_cast<uint32_t>(srcRect.width()),
                        static_cast<uint32_t>(srcRect.height()), 1};

  vkCmdCopyImageToBuffer(commandBuffer, vulkanSrc->vulkanImage(),
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vulkanDst->vulkanBuffer(), 1,
                         &region);
}

void VulkanCommandEncoder::generateMipmapsForTexture(std::shared_ptr<Texture>) {
  // TODO: Implement mipmap generation via vkCmdBlitImage chain.
}

std::shared_ptr<CommandBuffer> VulkanCommandEncoder::onFinish() {
  vkEndCommandBuffer(commandBuffer);
  return std::make_shared<VulkanCommandBuffer>(commandBuffer, commandPool);
}

}  // namespace tgfx
