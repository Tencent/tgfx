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

// Inserts a full pipeline barrier to transition an image between layouts. Uses broad stage and
// access masks to ensure correctness without fine-grained synchronization tracking.
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
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &barrier);
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
  result = vkBeginCommandBuffer(cmdBuffer, &beginInfo);
  if (result != VK_SUCCESS) {
    LOGE("VulkanCommandEncoder: vkBeginCommandBuffer failed.");
    vkDestroyCommandPool(gpu->device(), pool, nullptr);
    return nullptr;
  }

  auto encoder = gpu->makeResource<VulkanCommandEncoder>(gpu, cmdBuffer, pool);
  encoder->session.descriptorPool = gpu->acquireDescriptorPool();
  if (encoder->session.descriptorPool == VK_NULL_HANDLE) {
    LOGE("VulkanCommandEncoder: failed to acquire descriptor pool.");
  }
  return encoder;
}

VulkanCommandEncoder::VulkanCommandEncoder(VulkanGPU* gpu, VkCommandBuffer commandBuffer,
                                           VkCommandPool commandPool)
    : _gpu(gpu) {
  session.commandBuffer = commandBuffer;
  session.commandPool = commandPool;
}

void VulkanCommandEncoder::onRelease(VulkanGPU* gpu) {
  // If onFinish() was called, the session has already been moved to VulkanCommandBuffer.
  // This path only handles abandoned encoders (encoding was started but never finished).
  if (session.commandPool == VK_NULL_HANDLE) {
    return;
  }
  gpu->reclaimAbandonedSession(std::move(session));
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
  auto commandBuffer = session.commandBuffer;
  auto vulkanSrc = std::static_pointer_cast<VulkanTexture>(srcTexture);
  auto vulkanDst = std::static_pointer_cast<VulkanTexture>(dstTexture);
  retainResource(vulkanSrc);
  retainResource(vulkanDst);

  // Clamp copy region to source image bounds.
  auto srcX = static_cast<int32_t>(srcRect.x());
  auto srcY = static_cast<int32_t>(srcRect.y());
  auto copyWidth = static_cast<uint32_t>(srcRect.width());
  auto copyHeight = static_cast<uint32_t>(srcRect.height());
  auto srcW = static_cast<uint32_t>(srcTexture->width());
  auto srcH = static_cast<uint32_t>(srcTexture->height());
  if (srcX + copyWidth > srcW) {
    copyWidth = srcW > static_cast<uint32_t>(srcX) ? srcW - static_cast<uint32_t>(srcX) : 0;
  }
  if (srcY + copyHeight > srcH) {
    copyHeight = srcH > static_cast<uint32_t>(srcY) ? srcH - static_cast<uint32_t>(srcY) : 0;
  }
  if (copyWidth == 0 || copyHeight == 0) {
    // Even if nothing is copied, ensure dst layout is valid for subsequent use.
    if (vulkanDst->currentLayout() == VK_IMAGE_LAYOUT_UNDEFINED) {
      TransitionImageLayout(commandBuffer, vulkanDst->vulkanImage(), VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_GENERAL);
      vulkanDst->setCurrentLayout(VK_IMAGE_LAYOUT_GENERAL);
    }
    return;
  }

  TransitionImageLayout(commandBuffer, vulkanSrc->vulkanImage(), vulkanSrc->currentLayout(),
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  TransitionImageLayout(commandBuffer, vulkanDst->vulkanImage(), vulkanDst->currentLayout(),
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  VkImageCopy region = {};
  region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.srcOffset = {srcX, srcY, 0};
  region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.dstOffset = {static_cast<int32_t>(dstOffset.x), static_cast<int32_t>(dstOffset.y), 0};
  region.extent = {copyWidth, copyHeight, 1};

  vkCmdCopyImage(commandBuffer, vulkanSrc->vulkanImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 vulkanDst->vulkanImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  TransitionImageLayout(commandBuffer, vulkanSrc->vulkanImage(),
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
  TransitionImageLayout(commandBuffer, vulkanDst->vulkanImage(),
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

  vulkanSrc->setCurrentLayout(VK_IMAGE_LAYOUT_GENERAL);
  vulkanDst->setCurrentLayout(VK_IMAGE_LAYOUT_GENERAL);
}

void VulkanCommandEncoder::copyTextureToBuffer(std::shared_ptr<Texture> srcTexture,
                                               const Rect& srcRect,
                                               std::shared_ptr<GPUBuffer> dstBuffer,
                                               size_t dstOffset, size_t dstRowBytes) {
  if (!srcTexture || !dstBuffer) {
    return;
  }
  auto commandBuffer = session.commandBuffer;
  auto vulkanSrc = std::static_pointer_cast<VulkanTexture>(srcTexture);
  auto vulkanDst = std::static_pointer_cast<VulkanBuffer>(dstBuffer);
  retainResource(vulkanSrc);
  retainResource(vulkanDst);

  TransitionImageLayout(commandBuffer, vulkanSrc->vulkanImage(), vulkanSrc->currentLayout(),
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

  auto bytesPerPixel = VkFormatBytesPerPixel(vulkanSrc->vulkanFormat());
  DEBUG_ASSERT(dstRowBytes == 0 || dstRowBytes % bytesPerPixel == 0);
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

  TransitionImageLayout(commandBuffer, vulkanSrc->vulkanImage(),
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
  vulkanSrc->setCurrentLayout(VK_IMAGE_LAYOUT_GENERAL);
}

void VulkanCommandEncoder::generateMipmapsForTexture(std::shared_ptr<Texture> texture) {
  if (!texture) {
    return;
  }
  auto vulkanTexture = std::static_pointer_cast<VulkanTexture>(texture);
  if (vulkanTexture->mipLevelCount() <= 1) {
    return;
  }
  retainResource(vulkanTexture);

  auto commandBuffer = session.commandBuffer;
  auto image = vulkanTexture->vulkanImage();
  auto mipLevels = static_cast<uint32_t>(vulkanTexture->mipLevelCount());
  auto mipWidth = static_cast<int32_t>(vulkanTexture->width());
  auto mipHeight = static_cast<int32_t>(vulkanTexture->height());

  // Transition mip level 0 to TRANSFER_SRC (it contains the source data).
  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.subresourceRange.levelCount = 1;

  barrier.subresourceRange.baseMipLevel = 0;
  barrier.oldLayout = vulkanTexture->currentLayout();
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

  for (uint32_t i = 1; i < mipLevels; i++) {
    int32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
    int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;

    // Transition mip level i to TRANSFER_DST.
    barrier.subresourceRange.baseMipLevel = i;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Blit from mip level (i-1) to mip level i.
    VkImageBlit blit = {};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {nextWidth, nextHeight, 1};

    vkCmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    // Transition mip level i from TRANSFER_DST to TRANSFER_SRC for the next iteration.
    barrier.subresourceRange.baseMipLevel = i;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    mipWidth = nextWidth;
    mipHeight = nextHeight;
  }

  // Transition all mip levels to GENERAL for subsequent shader reads.
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mipLevels;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);

  vulkanTexture->setCurrentLayout(VK_IMAGE_LAYOUT_GENERAL);
}

std::shared_ptr<CommandBuffer> VulkanCommandEncoder::onFinish() {
  vkEndCommandBuffer(session.commandBuffer);
  return std::make_shared<VulkanCommandBuffer>(_gpu, std::move(session));
}

}  // namespace tgfx
