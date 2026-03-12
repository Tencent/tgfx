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

// MTLPixelFormat values (from Metal headers)
#define MTL_PIXEL_FORMAT_R8Unorm 10
#define MTL_PIXEL_FORMAT_RG8Unorm 30
#define MTL_PIXEL_FORMAT_RGBA8Unorm 70
#define MTL_PIXEL_FORMAT_BGRA8Unorm 80
#define MTL_PIXEL_FORMAT_Depth24Unorm_Stencil8 255
#define MTL_PIXEL_FORMAT_Depth32Float_Stencil8 260

inline PixelFormat MetalPixelFormatToPixelFormat(unsigned metalFormat) {
  switch (metalFormat) {
    case MTL_PIXEL_FORMAT_R8Unorm:
      return PixelFormat::ALPHA_8;
    case MTL_PIXEL_FORMAT_RG8Unorm:
      return PixelFormat::RG_88;
    case MTL_PIXEL_FORMAT_BGRA8Unorm:
      return PixelFormat::BGRA_8888;
    case MTL_PIXEL_FORMAT_Depth24Unorm_Stencil8:
    case MTL_PIXEL_FORMAT_Depth32Float_Stencil8:
      return PixelFormat::DEPTH24_STENCIL8;
    case MTL_PIXEL_FORMAT_RGBA8Unorm:
    default:
      return PixelFormat::RGBA_8888;
  }
}

}  // namespace tgfx
