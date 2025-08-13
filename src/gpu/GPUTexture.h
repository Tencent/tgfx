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

#include "gpu/GPUResource.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/PixelFormat.h"
#include "tgfx/platform/HardwareBuffer.h"

namespace tgfx {
/**
 * The type of GPUTexture. While only the 2D value is used by non-GL backends, the type must still
 * be known at the API-neutral layer to determine the legality of mipmapped, renderable, and
 * sampling parameters for proxies instantiated with wrapped textures.
 */
enum class TextureType { None, TwoD, Rectangle, External };

/**
 * GPUTexture represents a texture in the GPU backend for rendering operations.
 */
class GPUTexture : public GPUResource {
 public:
  /**
   * Returns the pixel format of the texture.
   */
  PixelFormat format() const {
    return _format;
  }

  /**
   * Returns the number of mipmap levels in the texture.
   */
  int mipLevelCount() const {
    return _mipLevelCount;
  }

  /**
   * The type of the texture.
   */
  virtual TextureType type() const {
    return TextureType::TwoD;
  }

  /**
   * Retrieves the backend texture with the specified size.
   */
  virtual BackendTexture getBackendTexture(int width, int height) const = 0;

  /**
   * Retrieves the backing hardware buffer. This method does not acquire any additional reference to
   * the returned hardware buffer. Returns nullptr if the texture is not created from a hardware
   * buffer.
   */
  virtual HardwareBufferRef getHardwareBuffer() const {
    return nullptr;
  }

  /**
   * Computes a key for the texture that can be used to identify it in a cache. The key is written
   * to the provided BytesKey object.
   */
  virtual void computeTextureKey(Context* context, BytesKey* bytesKey) const = 0;

 protected:
  PixelFormat _format = PixelFormat::RGBA_8888;
  int _mipLevelCount = 1;

  GPUTexture(PixelFormat format, int mipLevelCount)
      : _format(format), _mipLevelCount(mipLevelCount) {
  }
};
}  // namespace tgfx
