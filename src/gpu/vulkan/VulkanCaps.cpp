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

#include "VulkanCaps.h"
#include <cstring>
#include "gpu/vulkan/VulkanUtil.h"

namespace tgfx {

VulkanCaps::VulkanCaps(VkPhysicalDevice physicalDevice) {
  VkPhysicalDeviceProperties properties = {};
  vkGetPhysicalDeviceProperties(physicalDevice, &properties);

  _info.backend = Backend::Vulkan;
  _info.vendor = std::to_string(properties.vendorID);
  _info.renderer = properties.deviceName;
  _info.version = std::to_string(VK_API_VERSION_MAJOR(properties.apiVersion)) + "." +
                  std::to_string(VK_API_VERSION_MINOR(properties.apiVersion)) + "." +
                  std::to_string(VK_API_VERSION_PATCH(properties.apiVersion));

  initFeatures(physicalDevice);
  initLimits(physicalDevice);
  initFormatTable(physicalDevice);
}

bool VulkanCaps::isFormatRenderable(PixelFormat format) const {
  auto it = formatTable.find(format);
  return it != formatTable.end() && it->second.renderable;
}

bool VulkanCaps::isFormatAsColorAttachment(PixelFormat format) const {
  auto it = formatTable.find(format);
  return it != formatTable.end() && it->second.colorAttachment;
}

int VulkanCaps::getSampleCount(int requestedCount, PixelFormat format) const {
  if (requestedCount <= 1) {
    return 1;
  }
  auto it = formatTable.find(format);
  if (it == formatTable.end() || it->second.sampleCounts.empty()) {
    return 1;
  }
  for (int count : it->second.sampleCounts) {
    if (count >= requestedCount) {
      return count;
    }
  }
  return 1;
}

VkFormat VulkanCaps::getVkFormat(PixelFormat format) const {
  auto it = formatTable.find(format);
  if (it != formatTable.end()) {
    return it->second.vulkanFormat;
  }
  return VK_FORMAT_UNDEFINED;
}

void VulkanCaps::initFeatures(VkPhysicalDevice physicalDevice) {
  // Query device extensions to detect timeline semaphore support.
  uint32_t extensionCount = 0;
  vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
  std::vector<VkExtensionProperties> extensions(extensionCount);
  vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data());

  for (const auto& ext : extensions) {
    _info.extensions.emplace_back(ext.extensionName);
    if (strcmp(ext.extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0) {
      timelineSemaphoreSupported = true;
    }
  }

  _features.semaphore = timelineSemaphoreSupported;
  _features.clampToBorder = true;
  // Vulkan has no glTextureBarrier() equivalent. Disable to force the copy path for dst reads.
  _features.textureBarrier = false;

  // Check for framebuffer fetch support via rasterization order attachment access.
  frameBufferFetchSupported = false;
  for (const auto& ext : extensions) {
    if (strcmp(ext.extensionName, "VK_EXT_rasterization_order_attachment_access") == 0 ||
        strcmp(ext.extensionName, "VK_ARM_rasterization_order_attachment_access") == 0) {
      frameBufferFetchSupported = true;
      break;
    }
  }
}

void VulkanCaps::initLimits(VkPhysicalDevice physicalDevice) {
  VkPhysicalDeviceProperties properties = {};
  vkGetPhysicalDeviceProperties(physicalDevice, &properties);

  _limits.maxTextureDimension2D = static_cast<int>(properties.limits.maxImageDimension2D);
  _limits.maxSamplersPerShaderStage =
      static_cast<int>(properties.limits.maxPerStageDescriptorSamplers);
  _limits.maxUniformBufferBindingSize = static_cast<int>(properties.limits.maxUniformBufferRange);
  _limits.minUniformBufferOffsetAlignment =
      static_cast<int>(properties.limits.minUniformBufferOffsetAlignment);
}

VulkanCaps::FormatInfo VulkanCaps::checkFormat(VkPhysicalDevice physicalDevice,
                                               VkFormat vulkanFormat) const {
  FormatInfo info = {};
  info.vulkanFormat = vulkanFormat;

  VkFormatProperties formatProperties = {};
  vkGetPhysicalDeviceFormatProperties(physicalDevice, vulkanFormat, &formatProperties);

  VkFormatFeatureFlags optimalFlags = formatProperties.optimalTilingFeatures;

  if (optimalFlags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) {
    info.renderable = true;
    info.colorAttachment = true;
  } else if (optimalFlags & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
    info.renderable = true;
    info.colorAttachment = false;
  }

  if (info.renderable) {
    // Query supported MSAA sample counts from device limits.
    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    VkSampleCountFlags sampleCountFlags;
    if (info.colorAttachment) {
      sampleCountFlags = properties.limits.framebufferColorSampleCounts;
    } else {
      sampleCountFlags = properties.limits.framebufferDepthSampleCounts &
                         properties.limits.framebufferStencilSampleCounts;
    }

    for (int count : {1, 2, 4, 8, 16, 32, 64}) {
      if (sampleCountFlags & static_cast<VkSampleCountFlags>(count)) {
        info.sampleCounts.push_back(count);
      }
    }
  } else {
    info.sampleCounts = {1};
  }

  return info;
}

void VulkanCaps::initFormatTable(VkPhysicalDevice physicalDevice) {
  formatTable[PixelFormat::ALPHA_8] = checkFormat(physicalDevice, VK_FORMAT_R8_UNORM);
  formatTable[PixelFormat::GRAY_8] = checkFormat(physicalDevice, VK_FORMAT_R8_UNORM);
  formatTable[PixelFormat::RG_88] = checkFormat(physicalDevice, VK_FORMAT_R8G8_UNORM);
  formatTable[PixelFormat::RGBA_8888] = checkFormat(physicalDevice, VK_FORMAT_R8G8B8A8_UNORM);
  formatTable[PixelFormat::BGRA_8888] = checkFormat(physicalDevice, VK_FORMAT_B8G8R8A8_UNORM);

  // Prefer D24_UNORM_S8_UINT, fall back to D32_SFLOAT_S8_UINT if not supported.
  auto depthInfo = checkFormat(physicalDevice, VK_FORMAT_D24_UNORM_S8_UINT);
  if (!depthInfo.renderable) {
    depthInfo = checkFormat(physicalDevice, VK_FORMAT_D32_SFLOAT_S8_UINT);
  }
  formatTable[PixelFormat::DEPTH24_STENCIL8] = depthInfo;
}

}  // namespace tgfx
