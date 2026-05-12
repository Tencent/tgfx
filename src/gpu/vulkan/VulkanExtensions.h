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

  /// Queries extension availability and feature support from a physical device. Used when tgfx
  /// creates its own VkDevice.
  void query(VkPhysicalDevice physicalDevice) {
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, available.data());

    bool hasTimeline = false;
    bool hasDynState = false;
    for (const auto& ext : available) {
      if (strcmp(ext.extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0) {
        hasTimeline = true;
      }
      if (strcmp(ext.extensionName, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME) == 0) {
        hasDynState = true;
      }
    }

    // Verify actual feature support. Spec allows extension present + feature FALSE.
    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeature = {};
    timelineFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynStateFeature = {};
    dynStateFeature.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;

    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    if (hasTimeline) {
      timelineFeature.pNext = hasDynState ? &dynStateFeature : nullptr;
      features2.pNext = &timelineFeature;
    } else if (hasDynState) {
      features2.pNext = &dynStateFeature;
    }
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

    timelineSemaphore = hasTimeline && timelineFeature.timelineSemaphore;
    extendedDynamicState = hasDynState && dynStateFeature.extendedDynamicState;
  }

  /// Infers enabled extensions from volk function pointers. Used for externally created VkDevices
  /// (MakeFrom path) where we cannot know which extensions the host app enabled at creation time.
  void detectFromDevice() {
    timelineSemaphore = (vkGetSemaphoreCounterValueKHR != nullptr);
    extendedDynamicState = (vkCmdSetPrimitiveTopologyEXT != nullptr);
  }

  /// Returns the list of extension names to pass to VkDeviceCreateInfo::ppEnabledExtensionNames.
  std::vector<const char*> getEnabledNames() const {
    std::vector<const char*> names;
    names.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (timelineSemaphore) {
      names.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    }
    if (extendedDynamicState) {
      names.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
    }
    return names;
  }

  /// Builds the VkPhysicalDeviceFeatures2 pNext chain for vkCreateDevice. The caller must keep
  /// the returned feature structs alive until vkCreateDevice returns. For simplicity, this method
  /// populates the provided output structs and chains them into features2.
  void buildFeatureChain(VkPhysicalDeviceFeatures2& features2,
                         VkPhysicalDeviceTimelineSemaphoreFeatures& timelineFeature,
                         VkPhysicalDeviceExtendedDynamicStateFeaturesEXT& dynStateFeature) const {
    features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    timelineFeature = {};
    timelineFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    timelineFeature.timelineSemaphore = timelineSemaphore ? VK_TRUE : VK_FALSE;
    dynStateFeature = {};
    dynStateFeature.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
    dynStateFeature.extendedDynamicState = extendedDynamicState ? VK_TRUE : VK_FALSE;

    if (timelineSemaphore) {
      timelineFeature.pNext = extendedDynamicState ? &dynStateFeature : nullptr;
      features2.pNext = &timelineFeature;
    } else if (extendedDynamicState) {
      features2.pNext = &dynStateFeature;
    }
  }
};

}  // namespace tgfx
