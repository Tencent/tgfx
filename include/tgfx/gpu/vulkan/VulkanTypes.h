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

#include <cstdint>

// When building with Vulkan backend enabled, include the official Vulkan header to get the real
// type definitions. Otherwise, provide lightweight forward declarations so external code can use
// VulkanTypes.h without depending on the Vulkan SDK.
#ifdef TGFX_USE_VULKAN
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#else
typedef struct VkImage_T* VkImage;
typedef struct VkSemaphore_T* VkSemaphore;
typedef uint32_t VkFormat;
typedef uint32_t VkImageLayout;
typedef uint32_t VkImageUsageFlags;
#endif

namespace tgfx {

/**
 * Describes a Vulkan texture for external import/export.
 */
struct VulkanTextureInfo {
  /**
   * The VkImage handle.
   */
  VkImage image = nullptr;

  /**
   * The VkFormat of the image (VK_FORMAT_UNDEFINED = 0).
   */
  VkFormat format = static_cast<VkFormat>(0);

  /**
   * The current VkImageLayout (VK_IMAGE_LAYOUT_UNDEFINED = 0).
   */
  VkImageLayout layout = static_cast<VkImageLayout>(0);

  /**
   * The VkImageUsageFlags describing how the image is used.
   */
  VkImageUsageFlags usage = 0;
};

/**
 * Describes a Vulkan image used as a render target.
 */
struct VulkanImageInfo {
  /**
   * The VkImage handle.
   */
  VkImage image = nullptr;

  /**
   * The VkFormat of the image (VK_FORMAT_UNDEFINED = 0).
   */
  VkFormat format = static_cast<VkFormat>(0);

  /**
   * The current VkImageLayout (VK_IMAGE_LAYOUT_UNDEFINED = 0).
   */
  VkImageLayout layout = static_cast<VkImageLayout>(0);
};

/**
 * Describes a Vulkan timeline semaphore for synchronization.
 */
struct VulkanSyncInfo {
  /**
   * The VkSemaphore handle for a timeline semaphore.
   */
  VkSemaphore semaphore = nullptr;

  /**
   * The timeline value to signal or wait on.
   */
  uint64_t value = 0;
};

}  // namespace tgfx
