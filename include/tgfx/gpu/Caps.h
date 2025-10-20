/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
class Swizzle;
class ShaderCaps;

/**
 * Caps describes the capabilities of the GPU.
 */
class Caps {
 public:
  virtual ~Caps() = default;

  virtual const ShaderCaps* shaderCaps() const = 0;

  virtual const Swizzle& getReadSwizzle(PixelFormat pixelFormat) const = 0;

  virtual const Swizzle& getWriteSwizzle(PixelFormat pixelFormat) const = 0;

  virtual bool isFormatRenderable(PixelFormat pixelFormat) const = 0;

  virtual int getSampleCount(int requestedCount, PixelFormat pixelFormat) const = 0;

  int maxTextureSize = 0;
  /**
   * All backends support GPUFence, except for WebGPU.
   */
  bool fenceSupport = false;
  bool multisampleDisableSupport = false;
  /**
   * The CLAMP_TO_BORDER wrap mode for texture coordinates was added to desktop GL in 1.3, and
   * GLES 3.2, but is also available in extensions. Vulkan and Metal always have support.
   */
  bool clampToBorderSupport = false;
  bool textureBarrierSupport = false;
};
}  // namespace tgfx
