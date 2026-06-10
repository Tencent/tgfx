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

static constexpr int TEXTURE_BINDING_POINT_START = 2;

// WGPUTextureFormat values copied from webgpu.h (Emscripten SDK).
// These must match the WGPUTextureFormat enum in <webgpu/webgpu.h>.
// If Emscripten updates enum values, compile will fail at static_assert in WebGPUTypes.h.
#define WGPU_TEXTURE_FORMAT_R8Unorm 0x01
#define WGPU_TEXTURE_FORMAT_RG8Unorm 0x04
#define WGPU_TEXTURE_FORMAT_RGBA8Unorm 0x12
#define WGPU_TEXTURE_FORMAT_RGBA8UnormSrgb 0x13
#define WGPU_TEXTURE_FORMAT_BGRA8Unorm 0x17
#define WGPU_TEXTURE_FORMAT_BGRA8UnormSrgb 0x18
#define WGPU_TEXTURE_FORMAT_Depth24PlusStencil8 0x28
#define WGPU_TEXTURE_FORMAT_Depth32FloatStencil8 0x2A

inline PixelFormat WebGPUTextureFormatToPixelFormat(unsigned webgpuFormat) {
  switch (webgpuFormat) {
    case WGPU_TEXTURE_FORMAT_R8Unorm:
      return PixelFormat::ALPHA_8;
    case WGPU_TEXTURE_FORMAT_RG8Unorm:
      return PixelFormat::RG_88;
    case WGPU_TEXTURE_FORMAT_BGRA8Unorm:
    case WGPU_TEXTURE_FORMAT_BGRA8UnormSrgb:
      return PixelFormat::BGRA_8888;
    case WGPU_TEXTURE_FORMAT_Depth24PlusStencil8:
    case WGPU_TEXTURE_FORMAT_Depth32FloatStencil8:
      return PixelFormat::DEPTH24_STENCIL8;
    case WGPU_TEXTURE_FORMAT_RGBA8Unorm:
    case WGPU_TEXTURE_FORMAT_RGBA8UnormSrgb:
    default:
      return PixelFormat::RGBA_8888;
  }
}

}  // namespace tgfx
