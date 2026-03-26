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

// DXGI_FORMAT values (from dxgiformat.h). Defined as constexpr constants instead of #define macros
// to avoid conflicts with the Windows SDK's DXGI_FORMAT enum when both headers are included.
static constexpr unsigned DXGI_FORMAT_UNKNOWN = 0;
static constexpr unsigned DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20;
static constexpr unsigned DXGI_FORMAT_R8G8B8A8_UNORM = 28;
static constexpr unsigned DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29;
static constexpr unsigned DXGI_FORMAT_D24_UNORM_S8_UINT = 45;
static constexpr unsigned DXGI_FORMAT_R8G8_UNORM = 49;
static constexpr unsigned DXGI_FORMAT_R8_UNORM = 61;
static constexpr unsigned DXGI_FORMAT_A8_UNORM = 65;
static constexpr unsigned DXGI_FORMAT_B8G8R8A8_UNORM = 87;
static constexpr unsigned DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91;

inline PixelFormat DXGIFormatToPixelFormat(unsigned dxgiFormat) {
  switch (dxgiFormat) {
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_A8_UNORM:
      return PixelFormat::ALPHA_8;
    case DXGI_FORMAT_R8G8_UNORM:
      return PixelFormat::RG_88;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      return PixelFormat::BGRA_8888;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
      return PixelFormat::DEPTH24_STENCIL8;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    default:
      return PixelFormat::RGBA_8888;
  }
}

inline unsigned PixelFormatToDXGIFormat(PixelFormat format) {
  switch (format) {
    case PixelFormat::ALPHA_8:
      return DXGI_FORMAT_R8_UNORM;
    case PixelFormat::GRAY_8:
      return DXGI_FORMAT_R8_UNORM;
    case PixelFormat::RG_88:
      return DXGI_FORMAT_R8G8_UNORM;
    case PixelFormat::BGRA_8888:
      return DXGI_FORMAT_B8G8R8A8_UNORM;
    case PixelFormat::DEPTH24_STENCIL8:
      return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case PixelFormat::RGBA_8888:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    default:
      return DXGI_FORMAT_UNKNOWN;
  }
}

inline size_t DXGIFormatBytesPerPixel(unsigned dxgiFormat) {
  switch (dxgiFormat) {
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_A8_UNORM:
      return 1;
    case DXGI_FORMAT_R8G8_UNORM:
      return 2;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
      return 4;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
      return 8;
    default:
      return 4;
  }
}

}  // namespace tgfx
