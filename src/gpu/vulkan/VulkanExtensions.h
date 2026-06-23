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

  /// Queries extension availability and feature support from a physical device.
  void query(VkPhysicalDevice physicalDevice);

  /// Infers enabled extensions from volk function pointers. Used for externally created VkDevices.
  /// The physicalDevice is used to verify feature support (e.g. YCbCr conversion).
  void detectFromDevice(VkPhysicalDevice physicalDevice);

  /// Returns the list of extension names to pass to VkDeviceCreateInfo::ppEnabledExtensionNames.
  std::vector<const char*> getEnabledNames() const;

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
  void buildFeatureChain(FeatureChain& chain) const;

 private:
  const char* _roaaExtensionName = nullptr;
};

}  // namespace tgfx
