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

// VkFormat values from the Vulkan spec. Duplicated here so that Backend.cpp can perform format
// conversion without depending on vulkan.h or volk.h.
static constexpr unsigned VK_FORMAT_R8_UNORM_VALUE = 9;
static constexpr unsigned VK_FORMAT_R8G8_UNORM_VALUE = 16;
static constexpr unsigned VK_FORMAT_R8G8B8A8_UNORM_VALUE = 37;
static constexpr unsigned VK_FORMAT_R8G8B8A8_SRGB_VALUE = 43;
static constexpr unsigned VK_FORMAT_B8G8R8A8_UNORM_VALUE = 44;
static constexpr unsigned VK_FORMAT_B8G8R8A8_SRGB_VALUE = 50;
static constexpr unsigned VK_FORMAT_D24_UNORM_S8_UINT_VALUE = 129;
static constexpr unsigned VK_FORMAT_D32_SFLOAT_S8_UINT_VALUE = 130;

inline PixelFormat VulkanFormatToPixelFormat(unsigned vulkanFormat) {
  switch (vulkanFormat) {
    case VK_FORMAT_R8_UNORM_VALUE:
      return PixelFormat::ALPHA_8;
    case VK_FORMAT_R8G8_UNORM_VALUE:
      return PixelFormat::RG_88;
    case VK_FORMAT_B8G8R8A8_UNORM_VALUE:
    case VK_FORMAT_B8G8R8A8_SRGB_VALUE:
      return PixelFormat::BGRA_8888;
    case VK_FORMAT_D24_UNORM_S8_UINT_VALUE:
    case VK_FORMAT_D32_SFLOAT_S8_UINT_VALUE:
      return PixelFormat::DEPTH24_STENCIL8;
    case VK_FORMAT_R8G8B8A8_UNORM_VALUE:
    case VK_FORMAT_R8G8B8A8_SRGB_VALUE:
      return PixelFormat::RGBA_8888;
    default:
      return PixelFormat::Unknown;
  }
}

}  // namespace tgfx
