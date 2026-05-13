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
 * Describes a Vulkan timeline semaphore for synchronization.
 */
struct VulkanSyncInfo {
  /**
   * The VkSemaphore handle for a timeline semaphore.
   */
  uint64_t semaphore = 0;

  /**
   * The timeline value to signal or wait on.
   */
  uint64_t value = 0;
};

static_assert(std::is_trivially_copyable_v<VulkanImageInfo>);
static_assert(std::is_trivially_copyable_v<VulkanSyncInfo>);

}  // namespace tgfx
