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

#include "tgfx/gpu/PixelFormat.h"

namespace tgfx {

// VkFormat values (from Vulkan spec). These are duplicated here so that Backend.cpp can perform
// format conversion without depending on vulkan.h or volk.h.
#define TGFX_VK_FORMAT_UNDEFINED 0
#define TGFX_VK_FORMAT_R8_UNORM 9
#define TGFX_VK_FORMAT_R8G8_UNORM 16
#define TGFX_VK_FORMAT_R8G8B8A8_UNORM 37
#define TGFX_VK_FORMAT_R8G8B8A8_SRGB 43
#define TGFX_VK_FORMAT_B8G8R8A8_UNORM 44
#define TGFX_VK_FORMAT_B8G8R8A8_SRGB 50
#define TGFX_VK_FORMAT_D24_UNORM_S8_UINT 129
#define TGFX_VK_FORMAT_D32_SFLOAT_S8_UINT 130

inline PixelFormat VulkanFormatToPixelFormat(unsigned vulkanFormat) {
  switch (vulkanFormat) {
    case TGFX_VK_FORMAT_R8_UNORM:
      return PixelFormat::ALPHA_8;
    case TGFX_VK_FORMAT_R8G8_UNORM:
      return PixelFormat::RG_88;
    case TGFX_VK_FORMAT_B8G8R8A8_UNORM:
    case TGFX_VK_FORMAT_B8G8R8A8_SRGB:
      return PixelFormat::BGRA_8888;
    case TGFX_VK_FORMAT_D24_UNORM_S8_UINT:
    case TGFX_VK_FORMAT_D32_SFLOAT_S8_UINT:
      return PixelFormat::DEPTH24_STENCIL8;
    case TGFX_VK_FORMAT_R8G8B8A8_UNORM:
    case TGFX_VK_FORMAT_R8G8B8A8_SRGB:
    default:
      return PixelFormat::RGBA_8888;
  }
}

}  // namespace tgfx
