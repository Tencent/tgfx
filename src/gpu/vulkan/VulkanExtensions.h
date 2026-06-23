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

#pragma once

#include <cstring>
#include <vector>
#include "gpu/vulkan/VulkanAPI.h"

namespace tgfx {

/**
 * Centralized registry of Vulkan device extensions used by the tgfx Vulkan backend.
 *
 * Provides two initialization paths:
 *   - query(): for self-created devices — enumerates available extensions, queries feature support
 *     via vkGetPhysicalDeviceFeatures2, and populates boolean flags.
 *   - detectFromDevice(): for externally created devices (MakeFrom) — infers enabled extensions
 *     from volk function pointers loaded by volkLoadDevice().
 *
 * Adding a new extension requires only:
 *   1. Add a bool field here.
 *   2. Add discovery logic in query() and detectFromDevice().
 *   3. Add the extension name in getEnabledNames().
 *   4. Add the feature struct in buildFeatureChain() if applicable.
 */
struct VulkanExtensions {
  bool timelineSemaphore = false;
  bool extendedDynamicState = false;
  bool rasterizationOrderAttachmentAccess = false;
  /// Whether VK_KHR_swapchain is available. Headless drivers (e.g. SwiftShader) do not expose it.
  bool swapchain = false;
#if defined(__ANDROID__)
  /// Whether VK_ANDROID_external_memory_android_hardware_buffer is available.
  bool androidHardwareBuffer = false;
  /// Whether VK_KHR_sampler_ycbcr_conversion is available and the feature is supported.
  bool samplerYcbcrConversion = false;
#endif

 private:
  // Stores the actual ROAA extension name detected (EXT or ARM variant) for use in
  // getEnabledNames(). Null when ROAA is unavailable.
  const char* _roaaExtensionName = nullptr;

 public:
  /// Queries extension availability and feature support from a physical device. Used when tgfx
  /// creates its own VkDevice.
  void query(VkPhysicalDevice physicalDevice) {
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

    // Verify actual feature support. Spec allows extension present + feature FALSE.
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

    // Build pNext chain: timeline → dynState → roaa → ycbcr
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
    // Android HardwareBuffer import requires the full extension chain.
    androidHardwareBuffer =
        hasAndroidHwb && hasExternalMemory && hasDedicatedAllocation && hasQueueFamilyForeign;
    samplerYcbcrConversion = hasYcbcr && ycbcrFeature.samplerYcbcrConversion;
#endif
  }

  /// Infers enabled extensions from volk function pointers. Used for externally created VkDevices
  /// (MakeFrom path) where we cannot know which extensions the host app enabled at creation time.
  void detectFromDevice() {
    timelineSemaphore = (vkGetSemaphoreCounterValueKHR != nullptr);
    extendedDynamicState = (vkCmdSetPrimitiveTopologyEXT != nullptr);
    // ROAA has no unique entry points; cannot be inferred from function pointers.
    rasterizationOrderAttachmentAccess = false;
    // Swapchain can be detected from its unique entry point.
    swapchain = (vkCreateSwapchainKHR != nullptr);
#if defined(__ANDROID__)
    androidHardwareBuffer = (vkGetAndroidHardwareBufferPropertiesANDROID != nullptr);
    // YCbCr cannot be reliably inferred from function pointers alone; assume available if the
    // hardware buffer extension is present (the host app is responsible for enabling it).
    samplerYcbcrConversion = androidHardwareBuffer;
#endif
  }

  /// Returns the list of extension names to pass to VkDeviceCreateInfo::ppEnabledExtensionNames.
  std::vector<const char*> getEnabledNames() const {
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

  /// Holds all feature structs needed for vkCreateDevice. Declared by the caller on the stack and
  /// populated by buildFeatureChain(). The pNext chain is wired during buildFeatureChain() to
  /// ensure pointers point into the final object.
  struct FeatureChain {
    VkPhysicalDeviceFeatures2 features2 = {};
    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeature = {};
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynStateFeature = {};
#if defined(__ANDROID__)
    VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcrFeature = {};
#endif
  };

  /// Populates feature structs and wires the pNext chain inside the caller-owned FeatureChain.
  void buildFeatureChain(FeatureChain& chain) const {
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
      chain.ycbcrFeature.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES;
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
};

}  // namespace tgfx
