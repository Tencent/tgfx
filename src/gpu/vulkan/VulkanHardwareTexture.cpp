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

#if defined(__ANDROID__)

#include "VulkanHardwareTexture.h"
#include <android/hardware_buffer.h>
#include "VulkanGPU.h"
#include "VulkanUtil.h"
#include "core/utils/Log.h"

namespace tgfx {

static VkFormat HardwareBufferFormatToVkFormat(HardwareBufferFormat format) {
  switch (format) {
    case HardwareBufferFormat::RGBA_8888:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case HardwareBufferFormat::ALPHA_8:
      return VK_FORMAT_R8_UNORM;
    case HardwareBufferFormat::BGRA_8888:
      return VK_FORMAT_B8G8R8A8_UNORM;
    case HardwareBufferFormat::YCBCR_420_SP:
      return VK_FORMAT_UNDEFINED;
    default:
      return VK_FORMAT_UNDEFINED;
  }
}

static uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t memoryTypeBits,
                               VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties = {};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((memoryTypeBits & (1u << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if (memoryTypeBits & (1u << i)) {
      return i;
    }
  }
  return UINT32_MAX;
}

std::shared_ptr<VulkanHardwareTexture> VulkanHardwareTexture::MakeFrom(
    VulkanGPU* gpu, HardwareBufferRef hardwareBuffer, uint32_t usage) {
  if (!gpu || !hardwareBuffer) {
    return nullptr;
  }

  auto info = HardwareBufferGetInfo(hardwareBuffer);
  if (info.width <= 0 || info.height <= 0) {
    return nullptr;
  }

  bool isYUV = (info.format == HardwareBufferFormat::YCBCR_420_SP);
  if (isYUV && !gpu->extensions().samplerYcbcrConversion) {
    LOGE("VulkanHardwareTexture::MakeFrom() YUV buffer requires YCbCr conversion support.");
    return nullptr;
  }
  if (isYUV && (usage & TextureUsage::RENDER_ATTACHMENT)) {
    return nullptr;
  }

  VkDevice device = gpu->device();
  VkPhysicalDevice physDevice = gpu->physicalDevice();

  VkAndroidHardwareBufferFormatPropertiesANDROID formatProps = {};
  formatProps.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;

  VkAndroidHardwareBufferPropertiesANDROID hwbProps = {};
  hwbProps.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
  hwbProps.pNext = &formatProps;

  auto result = vkGetAndroidHardwareBufferPropertiesANDROID(device, hardwareBuffer, &hwbProps);
  if (result != VK_SUCCESS) {
    LOGE("VulkanHardwareTexture::MakeFrom() vkGetAndroidHardwareBufferPropertiesANDROID failed: %s",
         VkResultToString(result));
    return nullptr;
  }

  VkFormat vkFormat = formatProps.format;
  bool useExternalFormat = (vkFormat == VK_FORMAT_UNDEFINED);
  if (!useExternalFormat && !isYUV) {
    VkFormat expectedFormat = HardwareBufferFormatToVkFormat(info.format);
    if (expectedFormat != VK_FORMAT_UNDEFINED) {
      vkFormat = expectedFormat;
    }
  }

  VkExternalMemoryImageCreateInfo externalMemoryInfo = {};
  externalMemoryInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
  externalMemoryInfo.handleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

  VkExternalFormatANDROID externalFormat = {};
  externalFormat.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
  if (useExternalFormat) {
    externalFormat.externalFormat = formatProps.externalFormat;
    externalFormat.pNext = nullptr;
    externalMemoryInfo.pNext = &externalFormat;
  }

  VkImageUsageFlags vkUsage = VK_IMAGE_USAGE_SAMPLED_BIT;
  if (!useExternalFormat) {
    vkUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (usage & TextureUsage::RENDER_ATTACHMENT) {
      vkUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
  }

  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.pNext = &externalMemoryInfo;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = useExternalFormat ? VK_FORMAT_UNDEFINED : vkFormat;
  imageInfo.extent = {static_cast<uint32_t>(info.width), static_cast<uint32_t>(info.height), 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = vkUsage;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage vkImage = VK_NULL_HANDLE;
  result = vkCreateImage(device, &imageInfo, nullptr, &vkImage);
  if (result != VK_SUCCESS) {
    LOGE("VulkanHardwareTexture::MakeFrom() vkCreateImage failed: %s", VkResultToString(result));
    return nullptr;
  }

  VkImportAndroidHardwareBufferInfoANDROID importInfo = {};
  importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
  importInfo.buffer = hardwareBuffer;

  VkMemoryDedicatedAllocateInfo dedicatedInfo = {};
  dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
  dedicatedInfo.pNext = &importInfo;
  dedicatedInfo.image = vkImage;

  uint32_t memoryTypeIndex =
      FindMemoryType(physDevice, hwbProps.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (memoryTypeIndex == UINT32_MAX) {
    LOGE("VulkanHardwareTexture::MakeFrom() no suitable memory type found.");
    vkDestroyImage(device, vkImage, nullptr);
    return nullptr;
  }

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.pNext = &dedicatedInfo;
  allocInfo.allocationSize = hwbProps.allocationSize;
  allocInfo.memoryTypeIndex = memoryTypeIndex;

  VkDeviceMemory memory = VK_NULL_HANDLE;
  result = vkAllocateMemory(device, &allocInfo, nullptr, &memory);
  if (result != VK_SUCCESS) {
    LOGE("VulkanHardwareTexture::MakeFrom() vkAllocateMemory failed: %s", VkResultToString(result));
    vkDestroyImage(device, vkImage, nullptr);
    return nullptr;
  }

  result = vkBindImageMemory(device, vkImage, memory, 0);
  if (result != VK_SUCCESS) {
    LOGE("VulkanHardwareTexture::MakeFrom() vkBindImageMemory failed: %s",
         VkResultToString(result));
    vkFreeMemory(device, memory, nullptr);
    vkDestroyImage(device, vkImage, nullptr);
    return nullptr;
  }

  VkSamplerYcbcrConversion ycbcrConversion = VK_NULL_HANDLE;
  if (useExternalFormat) {
    VkSamplerYcbcrConversionCreateInfo ycbcrInfo = {};
    ycbcrInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
    ycbcrInfo.format = VK_FORMAT_UNDEFINED;
    ycbcrInfo.ycbcrModel = formatProps.suggestedYcbcrModel;
    ycbcrInfo.ycbcrRange = formatProps.suggestedYcbcrRange;
    ycbcrInfo.components = formatProps.samplerYcbcrConversionComponents;
    ycbcrInfo.xChromaOffset = formatProps.suggestedXChromaOffset;
    ycbcrInfo.yChromaOffset = formatProps.suggestedYChromaOffset;
    ycbcrInfo.chromaFilter = VK_FILTER_LINEAR;
    ycbcrInfo.forceExplicitReconstruction = VK_FALSE;

    VkExternalFormatANDROID ycbcrExternalFormat = {};
    ycbcrExternalFormat.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
    ycbcrExternalFormat.externalFormat = formatProps.externalFormat;
    ycbcrInfo.pNext = &ycbcrExternalFormat;

    result = vkCreateSamplerYcbcrConversion(device, &ycbcrInfo, nullptr, &ycbcrConversion);
    if (result != VK_SUCCESS) {
      LOGE("VulkanHardwareTexture::MakeFrom() vkCreateSamplerYcbcrConversion failed: %s",
           VkResultToString(result));
      vkFreeMemory(device, memory, nullptr);
      vkDestroyImage(device, vkImage, nullptr);
      return nullptr;
    }
  }

  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = vkImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = useExternalFormat ? VK_FORMAT_UNDEFINED : vkFormat;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkSamplerYcbcrConversionInfo ycbcrViewInfo = {};
  if (ycbcrConversion != VK_NULL_HANDLE) {
    ycbcrViewInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
    ycbcrViewInfo.conversion = ycbcrConversion;
    viewInfo.pNext = &ycbcrViewInfo;
  }

  VkImageView vkImageView = VK_NULL_HANDLE;
  result = vkCreateImageView(device, &viewInfo, nullptr, &vkImageView);
  if (result != VK_SUCCESS) {
    LOGE("VulkanHardwareTexture::MakeFrom() vkCreateImageView failed: %s",
         VkResultToString(result));
    if (ycbcrConversion != VK_NULL_HANDLE) {
      vkDestroySamplerYcbcrConversion(device, ycbcrConversion, nullptr);
    }
    vkFreeMemory(device, memory, nullptr);
    vkDestroyImage(device, vkImage, nullptr);
    return nullptr;
  }

  PixelFormat pixelFormat =
      useExternalFormat ? PixelFormat::RGBA_8888 : VkFormatToPixelFormat(vkFormat);
  TextureDescriptor descriptor = {};
  descriptor.width = info.width;
  descriptor.height = info.height;
  descriptor.format = pixelFormat;
  descriptor.usage = usage;

  VkFormat reportedFormat = useExternalFormat ? VK_FORMAT_R8G8B8A8_UNORM : vkFormat;

  return gpu->makeResource<VulkanHardwareTexture>(descriptor, hardwareBuffer, vkImage, vkImageView,
                                                  memory, ycbcrConversion, reportedFormat);
}

VulkanHardwareTexture::VulkanHardwareTexture(const TextureDescriptor& descriptor,
                                             HardwareBufferRef hardwareBuffer, VkImage image,
                                             VkImageView imageView, VkDeviceMemory dedicatedMemory,
                                             VkSamplerYcbcrConversion ycbcrConversion,
                                             VkFormat format)
    : VulkanTexture(descriptor, image, imageView, VK_NULL_HANDLE, VK_NULL_HANDLE, format, true,
                    VK_IMAGE_LAYOUT_UNDEFINED),
      hardwareBuffer(hardwareBuffer), dedicatedMemory(dedicatedMemory),
      ycbcrConversion(ycbcrConversion) {
  HardwareBufferRetain(hardwareBuffer);
}

VulkanHardwareTexture::~VulkanHardwareTexture() {
  HardwareBufferRelease(hardwareBuffer);
}

void VulkanHardwareTexture::onRelease(VulkanGPU* gpu) {
  VkDevice device = gpu->device();
  if (imageView != VK_NULL_HANDLE) {
    vkDestroyImageView(device, imageView, nullptr);
    imageView = VK_NULL_HANDLE;
  }
  if (ycbcrConversion != VK_NULL_HANDLE) {
    vkDestroySamplerYcbcrConversion(device, ycbcrConversion, nullptr);
    ycbcrConversion = VK_NULL_HANDLE;
  }
  if (image != VK_NULL_HANDLE) {
    vkDestroyImage(device, image, nullptr);
    image = VK_NULL_HANDLE;
  }
  if (dedicatedMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, dedicatedMemory, nullptr);
    dedicatedMemory = VK_NULL_HANDLE;
  }
}

}  // namespace tgfx

#endif
