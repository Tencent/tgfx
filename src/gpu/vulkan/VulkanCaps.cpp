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
#include "gpu/vulkan/VulkanUtil.h"

namespace tgfx {

VulkanCaps::VulkanCaps(VkPhysicalDevice physicalDevice, const VulkanExtensions& extensions) {
  VkPhysicalDeviceProperties properties = {};
  vkGetPhysicalDeviceProperties(physicalDevice, &properties);

  _info.backend = Backend::Vulkan;
  _info.vendor = std::to_string(properties.vendorID);
  _info.renderer = properties.deviceName;
  _info.version = std::to_string(VK_API_VERSION_MAJOR(properties.apiVersion)) + "." +
                  std::to_string(VK_API_VERSION_MINOR(properties.apiVersion)) + "." +
                  std::to_string(VK_API_VERSION_PATCH(properties.apiVersion));

  initFeatures(physicalDevice, extensions);
  initLimits(properties);
  initFormatTable(physicalDevice, properties);
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

void VulkanCaps::initFeatures(VkPhysicalDevice, const VulkanExtensions& extensions) {
  for (const auto& name : extensions.getEnabledNames()) {
    _info.extensions.emplace_back(name);
  }

  _features.semaphore = extensions.timelineSemaphore;
  _features.clampToBorder = true;
  // Vulkan has no glTextureBarrier() equivalent. Disable to force the copy path for dst reads.
  _features.textureBarrier = false;
  // stencilCoverPathSupported is set in initFormatTable() after probing the actual
  // depth/stencil format availability, because the feature must not be advertised when
  // no renderable D24S8 / D32S8 format exists on the device.

  frameBufferFetchSupported = extensions.rasterizationOrderAttachmentAccess;
}

void VulkanCaps::initLimits(const VkPhysicalDeviceProperties& properties) {
  _limits.maxTextureDimension2D = static_cast<int>(properties.limits.maxImageDimension2D);
  _limits.maxSamplersPerShaderStage =
      static_cast<int>(properties.limits.maxPerStageDescriptorSamplers);
  _limits.maxUniformBufferBindingSize = static_cast<int>(properties.limits.maxUniformBufferRange);
  _limits.minUniformBufferOffsetAlignment =
      static_cast<int>(properties.limits.minUniformBufferOffsetAlignment);
}

VulkanCaps::FormatInfo VulkanCaps::checkFormat(VkPhysicalDevice physicalDevice,
                                               VkFormat vulkanFormat,
                                               const VkPhysicalDeviceProperties& properties) const {
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

void VulkanCaps::initFormatTable(VkPhysicalDevice physicalDevice,
                                 const VkPhysicalDeviceProperties& properties) {
  formatTable[PixelFormat::ALPHA_8] = checkFormat(physicalDevice, VK_FORMAT_R8_UNORM, properties);
  formatTable[PixelFormat::GRAY_8] = checkFormat(physicalDevice, VK_FORMAT_R8_UNORM, properties);
  formatTable[PixelFormat::RG_88] = checkFormat(physicalDevice, VK_FORMAT_R8G8_UNORM, properties);
  formatTable[PixelFormat::RGBA_8888] =
      checkFormat(physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, properties);
  formatTable[PixelFormat::BGRA_8888] =
      checkFormat(physicalDevice, VK_FORMAT_B8G8R8A8_UNORM, properties);

  // Prefer D24_UNORM_S8_UINT, fall back to D32_SFLOAT_S8_UINT if not supported.
  // SwiftShader (vendorID 0x1AE0 = 6880 decimal) reports D24S8 as renderable via
  // vkGetPhysicalDeviceFormatProperties but does not actually support it at runtime,
  // emitting "UNSUPPORTED: format 37" warnings and segfaulting on stencil operations.
  // Skip D24 on SwiftShader and go straight to the D32 fallback.
  FormatInfo depthInfo = {};
  if (properties.vendorID != 0x1AE0) {
    depthInfo = checkFormat(physicalDevice, VK_FORMAT_D24_UNORM_S8_UINT, properties);
  }
  if (!depthInfo.renderable) {
    depthInfo = checkFormat(physicalDevice, VK_FORMAT_D32_SFLOAT_S8_UINT, properties);
  }
  formatTable[PixelFormat::DEPTH24_STENCIL8] = depthInfo;
  // Only advertise stencilCoverPath when a depth/stencil format is actually renderable.
  // SwiftShader (vendorID 0x1AE0) reports D24S8 as renderable but does not support it at
  // runtime, and D32S8 stencil operations also crash (SwiftShader internally falls back to
  // D24 and hits the same UNSUPPORTED path). Disable stencilCoverPath on SwiftShader until
  // the root cause of the D32 stencil segfault is fully investigated and fixed.
  _features.stencilCoverPathSupported = depthInfo.renderable && properties.vendorID != 0x1AE0;
}

}  // namespace tgfx
