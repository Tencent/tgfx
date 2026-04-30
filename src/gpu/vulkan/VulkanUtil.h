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

#include "core/utils/Log.h"
#include "gpu/vulkan/VulkanAPI.h"
#include "tgfx/gpu/PixelFormat.h"

namespace tgfx {

/// Converts a VkResult to a human-readable string.
const char* VkResultToString(VkResult result);

/// Converts a tgfx PixelFormat to the corresponding VkFormat.
VkFormat PixelFormatToVkFormat(PixelFormat format);

/// Converts a VkFormat to the corresponding tgfx PixelFormat.
PixelFormat VkFormatToPixelFormat(VkFormat format);

/// Returns true if the given VkFormat is a depth-stencil format.
bool VkFormatIsDepthStencil(VkFormat format);

/// Returns true if the given VkFormat is a stencil format.
bool VkFormatIsStencil(VkFormat format);

/// Returns the bytes per pixel for the given VkFormat.
int VkFormatBytesPerPixel(VkFormat format);

// Error checking macro that logs the VkResult string on failure.
#define VK_CHECK(result)                                                                   \
  do {                                                                                     \
    VkResult _vk_result = (result);                                                        \
    if (_vk_result != VK_SUCCESS) {                                                        \
      LOGE("Vulkan error: %s at %s:%d", VkResultToString(_vk_result), __FILE__, __LINE__); \
    }                                                                                      \
  } while (0)

// Error checking macro that returns false on failure.
#define VK_CHECK_RESULT(result)                                                            \
  do {                                                                                     \
    VkResult _vk_result = (result);                                                        \
    if (_vk_result != VK_SUCCESS) {                                                        \
      LOGE("Vulkan error: %s at %s:%d", VkResultToString(_vk_result), __FILE__, __LINE__); \
      return false;                                                                        \
    }                                                                                      \
  } while (0)

}  // namespace tgfx
