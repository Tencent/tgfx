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

#include "VulkanTexture.h"
#include "VulkanGPU.h"
#include "VulkanUtil.h"
#include "core/utils/Log.h"

namespace tgfx {

static VkImageUsageFlags ToVkImageUsage(uint32_t usage) {
  VkImageUsageFlags flags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (usage & TextureUsage::TEXTURE_BINDING) {
    flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
  }
  if (usage & TextureUsage::RENDER_ATTACHMENT) {
    flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  return flags;
}

static VkImageAspectFlags FormatToAspectFlags(VkFormat format) {
  if (VkFormatIsDepthStencil(format)) {
    return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  return VK_IMAGE_ASPECT_COLOR_BIT;
}

std::shared_ptr<VulkanTexture> VulkanTexture::Make(VulkanGPU* gpu,
                                                   const TextureDescriptor& descriptor) {
  if (!gpu || descriptor.width <= 0 || descriptor.height <= 0) {
    return nullptr;
  }

  VkFormat vkFormat = gpu->getVkFormat(descriptor.format);
  if (vkFormat == VK_FORMAT_UNDEFINED) {
    LOGE("VulkanTexture::Make() unsupported pixel format.");
    return nullptr;
  }

  bool isDepthStencil = (descriptor.format == PixelFormat::DEPTH24_STENCIL8);
  VkImageUsageFlags usageFlags = ToVkImageUsage(descriptor.usage);
  if (isDepthStencil) {
    usageFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }

  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = vkFormat;
  imageInfo.extent = {static_cast<uint32_t>(descriptor.width),
                      static_cast<uint32_t>(descriptor.height), 1};
  imageInfo.mipLevels = static_cast<uint32_t>(descriptor.mipLevelCount);
  imageInfo.arrayLayers = 1;
  DEBUG_ASSERT(descriptor.sampleCount >= 1 &&
               (descriptor.sampleCount & (descriptor.sampleCount - 1)) == 0);
  imageInfo.samples = static_cast<VkSampleCountFlagBits>(descriptor.sampleCount);
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = usageFlags;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo allocInfo = {};
  allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  VkImage vkImage = VK_NULL_HANDLE;
  VmaAllocation vmaAllocation = VK_NULL_HANDLE;
  auto result =
      vmaCreateImage(gpu->allocator(), &imageInfo, &allocInfo, &vkImage, &vmaAllocation, nullptr);
  if (result != VK_SUCCESS) {
    LOGE("VulkanTexture::Make() vmaCreateImage failed: %s", VkResultToString(result));
    return nullptr;
  }

  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = vkImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = vkFormat;
  viewInfo.subresourceRange.aspectMask = FormatToAspectFlags(vkFormat);
  viewInfo.subresourceRange.baseMipLevel = 0;
  // Framebuffer attachments require levelCount=1. Use full mip chain only for pure sampling views.
  viewInfo.subresourceRange.levelCount =
      (descriptor.usage & TextureUsage::RENDER_ATTACHMENT) ? 1
                                                           : static_cast<uint32_t>(descriptor.mipLevelCount);
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkImageView vkImageView = VK_NULL_HANDLE;
  result = vkCreateImageView(gpu->device(), &viewInfo, nullptr, &vkImageView);
  if (result != VK_SUCCESS) {
    LOGE("VulkanTexture::Make() vkCreateImageView failed: %s", VkResultToString(result));
    vmaDestroyImage(gpu->allocator(), vkImage, vmaAllocation);
    return nullptr;
  }

  return gpu->makeResource<VulkanTexture>(descriptor, vkImage, vkImageView, vmaAllocation,
                                          vkFormat, false);
}

std::shared_ptr<VulkanTexture> VulkanTexture::MakeFrom(VulkanGPU* gpu, VkImage image,
                                                       VkFormat format, int width, int height,
                                                       uint32_t usage, bool adopted) {
  if (!gpu || image == VK_NULL_HANDLE) {
    return nullptr;
  }

  TextureDescriptor descriptor = {};
  descriptor.width = width;
  descriptor.height = height;
  descriptor.format = VkFormatToPixelFormat(format);
  descriptor.usage = usage;

  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = FormatToAspectFlags(format);
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkImageView imageView = VK_NULL_HANDLE;
  auto result = vkCreateImageView(gpu->device(), &viewInfo, nullptr, &imageView);
  if (result != VK_SUCCESS) {
    LOGE("VulkanTexture::MakeFrom() vkCreateImageView failed: %s", VkResultToString(result));
    return nullptr;
  }

  return gpu->makeResource<VulkanTexture>(descriptor, image, imageView, VK_NULL_HANDLE, format,
                                          adopted);
}

VulkanTexture::VulkanTexture(const TextureDescriptor& descriptor, VkImage image,
                             VkImageView imageView, VmaAllocation allocation, VkFormat format,
                             bool adopted)
    : Texture(descriptor), image(image), imageView(imageView), allocation(allocation),
      format(format), adopted(adopted) {
}

void VulkanTexture::onRelease(VulkanGPU* gpu) {
  if (imageView != VK_NULL_HANDLE) {
    vkDestroyImageView(gpu->device(), imageView, nullptr);
    imageView = VK_NULL_HANDLE;
  }
  if (allocation != VK_NULL_HANDLE) {
    vmaDestroyImage(gpu->allocator(), image, allocation);
    image = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;
  } else if (adopted && image != VK_NULL_HANDLE) {
    vkDestroyImage(gpu->device(), image, nullptr);
    image = VK_NULL_HANDLE;
  }
}

BackendTexture VulkanTexture::getBackendTexture() const {
  if (image == VK_NULL_HANDLE || !(descriptor.usage & TextureUsage::TEXTURE_BINDING)) {
    return {};
  }
  VulkanTextureInfo vulkanInfo;
  vulkanInfo.image = reinterpret_cast<void*>(image);
  vulkanInfo.format = static_cast<uint32_t>(format);
  vulkanInfo.layout = static_cast<uint32_t>(layout);
  return BackendTexture(vulkanInfo, descriptor.width, descriptor.height);
}

BackendRenderTarget VulkanTexture::getBackendRenderTarget() const {
  if (image == VK_NULL_HANDLE || !(descriptor.usage & TextureUsage::RENDER_ATTACHMENT)) {
    return {};
  }
  VulkanImageInfo vulkanInfo;
  vulkanInfo.image = reinterpret_cast<void*>(image);
  vulkanInfo.format = static_cast<uint32_t>(format);
  vulkanInfo.layout = static_cast<uint32_t>(layout);
  return BackendRenderTarget(vulkanInfo, descriptor.width, descriptor.height);
}

}  // namespace tgfx
