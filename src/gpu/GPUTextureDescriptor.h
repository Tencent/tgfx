/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "tgfx/gpu/PixelFormat.h"

namespace tgfx {
/**
 * GPUTextureUsage defines the usage flags for GPU textures.
 * These flags indicate how the texture can be used in rendering operations.
 */
class GPUTextureUsage {
 public:
  /**
   * The texture can be bound for use as a sampled texture in a shader.
   */
  static constexpr uint32_t TEXTURE_BINDING = 0x04;

  /**
   * The texture can be used as a color or depth/stencil attachment in a render pass.
   */
  static constexpr uint32_t RENDER_ATTACHMENT = 0x10;
};

/**
 * GPUTextureDescriptor is used to describe the properties of a GPUTexture.
 */
class GPUTextureDescriptor {
 public:
  /**
   * Default constructor initializes the texture descriptor with default values.
   */
  GPUTextureDescriptor() = default;

  /**
   * Constructs a GPUTextureDescriptor with the specified properties.
   */
  GPUTextureDescriptor(int width, int height, PixelFormat format, bool mipmapped = false,
                       int sampleCount = 1, uint32_t usage = GPUTextureUsage::TEXTURE_BINDING);

  /**
   * The width of the texture in pixels.
   */
  int width = 0;

  /**
   * The height of the texture in pixels.
   */
  int height = 0;

  /**
   * The pixel format of the texture.
   */
  PixelFormat format = PixelFormat::RGBA_8888;

  /**
   * The number of mipmap levels in the texture. A value of 1 indicates no mipmaps.
   */
  int mipLevelCount = 1;

  /**
   * The number of samples per pixel in the texture. A value of 1 indicates no multisampling.
   */
  int sampleCount = 1;

  /**
   * The bitwise flags that indicate the usage options for the texture. The value is the sum of the
   * decimal values for each flag. See GPUTextureUsage for more details.
   */
  uint32_t usage = GPUTextureUsage::TEXTURE_BINDING;
};

}  // namespace tgfx
