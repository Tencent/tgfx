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

#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/PixelFormat.h"
#include "tgfx/platform/HardwareBuffer.h"

namespace tgfx {
/**
 * The type of Texture. While only the 2D value is used by non-GL backends, the type must still
 * be known at the API-neutral layer to determine the legality of mipmapped, renderable, and
 * sampling parameters for proxies instantiated with wrapped textures.
 */
enum class TextureType { None, TwoD, Rectangle, External };

/**
 * TextureUsage defines the usage flags for GPU textures. These flags indicate how the texture can
 * be used in rendering operations.
 */
class TextureUsage {
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
 * TextureDescriptor is used to describe the properties of a Texture.
 */
class TextureDescriptor {
 public:
  /**
   * Default constructor initializes the texture descriptor with default values.
   */
  TextureDescriptor() = default;

  /**
   * Constructs a TextureDescriptor with the specified properties.
   */
  TextureDescriptor(int width, int height, PixelFormat format, bool mipmapped = false,
                    int sampleCount = 1, uint32_t usage = TextureUsage::TEXTURE_BINDING);

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
   * decimal values for each flag. See TextureUsage for more details.
   */
  uint32_t usage = TextureUsage::TEXTURE_BINDING;
};

/**
 * Represents a texture in the GPU backend for rendering operations.
 */
class Texture {
 public:
  virtual ~Texture() = default;

  /**
   * Returns the width of the texture in pixels.
   */
  int width() const {
    return descriptor.width;
  }

  /**
   * Returns the height of the texture in pixels.
   */
  int height() const {
    return descriptor.height;
  }

  /**
   * Returns the pixel format of the texture.
   */
  PixelFormat format() const {
    return descriptor.format;
  }

  /**
   * Returns The number of samples per pixel in the texture. A value of 1 indicates no multisampling.
   */
  int sampleCount() const {
    return descriptor.sampleCount;
  }

  /**
   * Returns the number of mipmap levels in the texture.
   */
  int mipLevelCount() const {
    return descriptor.mipLevelCount;
  }

  /**
   * Returns the bitwise flags that indicate the original usage options set when the Texture was
   * created. The returned value is the sum of the decimal values for each flag. See TextureUsage
   * for more details.
   */
  uint32_t usage() const {
    return descriptor.usage;
  }

  /**
   * The type of the texture.
   */
  virtual TextureType type() const {
    return TextureType::TwoD;
  }

  /**
   * Retrieves the backend texture. An invalid BackendTexture will be returned if the texture
   * is not created with TextureUsage::TEXTURE_BINDING.
   */
  virtual BackendTexture getBackendTexture() const = 0;

  /**
   * Retrieves the backend render target. An invalid BackendRenderTarget will be returned if the
   * texture is not created with TextureUsage::RENDER_ATTACHMENT.
   */
  virtual BackendRenderTarget getBackendRenderTarget() const = 0;

  /**
   * Retrieves the backing hardware buffer. This method does not acquire any additional reference to
   * the returned hardware buffer. Returns nullptr if the texture is not created from a hardware
   * buffer.
   */
  virtual HardwareBufferRef getHardwareBuffer() const {
    return nullptr;
  }

 protected:
  TextureDescriptor descriptor = {};

  explicit Texture(const TextureDescriptor& descriptor) : descriptor(descriptor) {
  }
};
}  // namespace tgfx
