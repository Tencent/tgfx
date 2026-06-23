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

#include "VulkanExtensions.h"
#include <cstring>

namespace tgfx {

void VulkanExtensions::query(VkPhysicalDevice physicalDevice) {
  VkPhysicalDeviceProperties deviceProps = {};
  vkGetPhysicalDeviceProperties(physicalDevice, &deviceProps);
  bool isVulkan11OrLater = (deviceProps.apiVersion >= VK_API_VERSION_1_1);

  uint32_t extCount = 0;
  vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
  std::vector<VkExtensionProperties> available(extCount);
  vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, available.data());

  // VK_KHR_timeline_semaphore, VK_KHR_external_memory, VK_KHR_dedicated_allocation, and
  // VK_KHR_sampler_ycbcr_conversion are promoted to Vulkan 1.1 core. Drivers may omit them
  // from the enumeration list on 1.1+ devices, so treat them as available when the device
  // reports apiVersion >= 1.1.
  bool hasTimeline = isVulkan11OrLater;
  bool hasDynState = false;
  const char* roaaExtName = nullptr;
  bool hasSwapchain = false;
#if defined(__ANDROID__)
  bool hasExternalMemory = isVulkan11OrLater;
  bool hasDedicatedAllocation = isVulkan11OrLater;
  bool hasAndroidHwb = false;
  bool hasYcbcr = isVulkan11OrLater;
  bool hasQueueFamilyForeign = false;
#endif
  for (const auto& ext : available) {
    if (strcmp(ext.extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0) {
      hasTimeline = true;
    } else if (strcmp(ext.extensionName, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME) == 0) {
      hasDynState = true;
    } else if (strcmp(ext.extensionName, "VK_EXT_rasterization_order_attachment_access") == 0) {
      roaaExtName = "VK_EXT_rasterization_order_attachment_access";
    } else if (strcmp(ext.extensionName, "VK_ARM_rasterization_order_attachment_access") == 0) {
      if (!roaaExtName) {
        roaaExtName = "VK_ARM_rasterization_order_attachment_access";
      }
    } else if (strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
      hasSwapchain = true;
    }
#if defined(__ANDROID__)
    else if (strcmp(ext.extensionName, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) == 0) {
      hasExternalMemory = true;
    } else if (strcmp(ext.extensionName, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) == 0) {
      hasDedicatedAllocation = true;
    } else if (strcmp(ext.extensionName,
                      VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME) == 0) {
      hasAndroidHwb = true;
    } else if (strcmp(ext.extensionName, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME) == 0) {
      hasYcbcr = true;
    } else if (strcmp(ext.extensionName, VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME) == 0) {
      hasQueueFamilyForeign = true;
    }
#endif
  }

  VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeature = {};
  timelineFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;

  VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynStateFeature = {};
  dynStateFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;

  VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT roaaFeature = {};
  roaaFeature.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_FEATURES_EXT;

#if defined(__ANDROID__)
  VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcrFeature = {};
  ycbcrFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES;
#endif

  void* pNextChain = nullptr;
#if defined(__ANDROID__)
  if (hasYcbcr) {
    ycbcrFeature.pNext = pNextChain;
    pNextChain = &ycbcrFeature;
  }
#endif
  if (roaaExtName) {
    roaaFeature.pNext = pNextChain;
    pNextChain = &roaaFeature;
  }
  if (hasDynState) {
    dynStateFeature.pNext = pNextChain;
    pNextChain = &dynStateFeature;
  }
  if (hasTimeline) {
    timelineFeature.pNext = pNextChain;
    pNextChain = &timelineFeature;
  }

  VkPhysicalDeviceFeatures2 features2 = {};
  features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features2.pNext = pNextChain;
  vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

  timelineSemaphore = hasTimeline && timelineFeature.timelineSemaphore;
  extendedDynamicState = hasDynState && dynStateFeature.extendedDynamicState;
  rasterizationOrderAttachmentAccess =
      (roaaExtName != nullptr) && roaaFeature.rasterizationOrderColorAttachmentAccess;
  _roaaExtensionName = roaaExtName;
  swapchain = hasSwapchain;
#if defined(__ANDROID__)
  androidHardwareBuffer =
      hasAndroidHwb && hasExternalMemory && hasDedicatedAllocation && hasQueueFamilyForeign;
  samplerYcbcrConversion = hasYcbcr && ycbcrFeature.samplerYcbcrConversion;
#endif
}

void VulkanExtensions::detectFromDevice() {
  timelineSemaphore = (vkGetSemaphoreCounterValueKHR != nullptr);
  extendedDynamicState = (vkCmdSetPrimitiveTopologyEXT != nullptr);
  rasterizationOrderAttachmentAccess = false;
  swapchain = (vkCreateSwapchainKHR != nullptr);
#if defined(__ANDROID__)
  androidHardwareBuffer = (vkGetAndroidHardwareBufferPropertiesANDROID != nullptr);
  samplerYcbcrConversion = androidHardwareBuffer;
#endif
}

std::vector<const char*> VulkanExtensions::getEnabledNames() const {
  std::vector<const char*> names;
  if (swapchain) {
    names.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  }
  if (timelineSemaphore) {
    names.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
  }
  if (extendedDynamicState) {
    names.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
  }
  if (rasterizationOrderAttachmentAccess) {
    names.push_back(_roaaExtensionName);
  }
#if defined(__ANDROID__)
  if (androidHardwareBuffer) {
    names.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
    names.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
    names.push_back(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
    names.push_back(VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME);
  }
  if (samplerYcbcrConversion) {
    names.push_back(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
  }
#endif
  return names;
}

void VulkanExtensions::buildFeatureChain(FeatureChain& chain) const {
  chain.features2 = {};
  chain.features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  chain.timelineFeature = {};
  chain.timelineFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  chain.timelineFeature.timelineSemaphore = timelineSemaphore ? VK_TRUE : VK_FALSE;
  chain.dynStateFeature = {};
  chain.dynStateFeature.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
  chain.dynStateFeature.extendedDynamicState = extendedDynamicState ? VK_TRUE : VK_FALSE;

  void* pNextChain = nullptr;
#if defined(__ANDROID__)
  if (samplerYcbcrConversion) {
    chain.ycbcrFeature = {};
    chain.ycbcrFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES;
    chain.ycbcrFeature.samplerYcbcrConversion = VK_TRUE;
    chain.ycbcrFeature.pNext = pNextChain;
    pNextChain = &chain.ycbcrFeature;
  }
#endif
  if (extendedDynamicState) {
    chain.dynStateFeature.pNext = pNextChain;
    pNextChain = &chain.dynStateFeature;
  }
  if (timelineSemaphore) {
    chain.timelineFeature.pNext = pNextChain;
    pNextChain = &chain.timelineFeature;
  }
  chain.features2.pNext = pNextChain;
}

}  // namespace tgfx
