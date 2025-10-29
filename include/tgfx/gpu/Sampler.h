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

#include "tgfx/gpu/FilterMode.h"
#include "tgfx/gpu/MipmapMode.h"

namespace tgfx {
/**
 * AddressMode defines how texture coordinates outside the range [0, 1] are handled.
 */
enum class AddressMode {
  /**
   * Texture coordinates are clamped between 0.0 and 1.0, inclusive.
   */
  ClampToEdge,

  /**
   * Texture coordinates wrap to the other side of the texture, effectively keeping only the
   * fractional part of the texture coordinate.
   */
  Repeat,

  /**
   * Between -1.0 and 1.0, the texture coordinates are mirrored across the axis; outside -1.0 and
   * 1.0, the image is repeated.
   */
  MirrorRepeat,

  /**
   * Out-of-range texture coordinates return transparent zero (0,0,0,0) for images with an alpha
   * channel and return opaque zero (0,0,0,1) for images without an alpha channel.
   */
  ClampToBorder
};

/**
 * An object that you use to configure a texture sampler.
 */
class SamplerDescriptor {
 public:
  /**
   * Constructs a sampler descriptor with default parameters.
   */
  SamplerDescriptor() = default;

  /**
   * Constructs a sampler descriptor with the specified address modes, filter modes, and mipmap
   * modes.
   */
  SamplerDescriptor(AddressMode addressModeX, AddressMode addressModeY, FilterMode minFilter,
                    FilterMode magFilter, MipmapMode mipmapMode)
      : addressModeX(addressModeX), addressModeY(addressModeY), minFilter(minFilter),
        magFilter(magFilter), mipmapMode(mipmapMode) {
  }

  /**
   * The address mode for the texture width coordinate.
   */
  AddressMode addressModeX = AddressMode::ClampToEdge;

  /**
   * The address mode for the texture height coordinate.
   */
  AddressMode addressModeY = AddressMode::ClampToEdge;

  /**
   * The filter mode to use when the texture is minified.
   */
  FilterMode minFilter = FilterMode::Nearest;

  /**
   * The filter mode to use when the texture is magnified.
   */
  FilterMode magFilter = FilterMode::Nearest;

  /**
   * The mipmap mode used when sampling between different mipmap levels.
   */
  MipmapMode mipmapMode = MipmapMode::None;
};

/**
 * Sampler encapsulates the sampling state for a texture. It defines how texture coordinates outside
 * the range [0, 1] are handled (wrap mode), and how the texture is filtered when it is minified or
 * magnified (filter mode).
 */
class Sampler {
 public:
  virtual ~Sampler() = default;
};
}  // namespace tgfx
