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
#include <type_traits>

namespace tgfx {

/**
 * Describes a Vulkan image for external import/export. Used by both BackendTexture and
 * BackendRenderTarget, following the same unified pattern as MetalTextureInfo.
 */
struct VulkanImageInfo {
  /**
   * The VkImage handle.
   */
  uint64_t image = 0;

  /**
   * The VkFormat of the image (VK_FORMAT_UNDEFINED = 0).
   */
  uint32_t format = 0;

  /**
   * The current VkImageLayout (VK_IMAGE_LAYOUT_UNDEFINED = 0).
   */
  uint32_t layout = 0;
};

/**
 * Describes a Vulkan semaphore for synchronization. Supports both binary and timeline semaphores.
 */
struct VulkanSyncInfo {
  /**
   * The VkSemaphore handle (cast to uint64_t). Can be either a binary or timeline semaphore.
   */
  uint64_t semaphore = 0;

  /**
   * For timeline semaphores, the value to signal or wait on (must be > 0). For binary semaphores,
   * set to 0. When value is 0, the semaphore is treated as binary; when > 0, it is treated as a
   * timeline semaphore.
   */
  uint64_t value = 0;
};

static_assert(std::is_trivially_copyable_v<VulkanImageInfo>);
static_assert(std::is_trivially_copyable_v<VulkanSyncInfo>);
static_assert(std::is_standard_layout_v<VulkanImageInfo>);
static_assert(std::is_standard_layout_v<VulkanSyncInfo>);

}  // namespace tgfx
