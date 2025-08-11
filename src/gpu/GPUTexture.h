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
#include "gpu/YUVFormat.h"
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
   * Returns the PixelFormat of the backend texture. If the backendTexture is invalid, returns
   * PixelFormat::Unknown.
   */
  static PixelFormat GetPixelFormat(const BackendTexture& backendTexture);

  /**
   * Creates a new GPUTexture that wraps the given backend texture. If adopted is true, the
   * GPUTexture will take ownership of the backend texture and destroy it when no longer needed.
   * Otherwise, the backend texture must remain valid for as long as the GPUTexture exists.
   */
  static std::unique_ptr<GPUTexture> MakeFrom(Context* context,
                                              const BackendTexture& backendTexture,
                                              bool adopted = false);

  /**
   * Creates a new GPUTexture with the given width, height, and pixel format. If mipmapped is
   * true, mipmap levels will be generated. Returns nullptr if the texture cannot be created.
   */
  static std::unique_ptr<GPUTexture> Make(Context* context, int width, int height,
                                          PixelFormat format = PixelFormat::RGBA_8888,
                                          bool mipmapped = false);

  /**
   * Returns the pixel format of the texture.
   */
  PixelFormat format() const {
    return _format;
  }

  /**
   * Returns the maximum mipmap level of the texture.
   */
  int maxMipmapLevel() const {
    return _maxMipmapLevel;
  }

  /**
   * Returns true if the texture has mipmap levels.
   */
  bool hasMipmaps() const {
    return _maxMipmapLevel > 0;
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
   * Writes pixel data to the texture within the specified rectangle. The pixel data must match the
   * texture's pixel format, and the rectangle must be fully contained within the texture's
   * dimensions. If the texture has mipmaps, you must call regenerateMipmapLevels() after writing
   * pixels; this will not happen automatically.
   */
  virtual void writePixels(Context* context, const Rect& rect, const void* pixels,
                           size_t rowBytes) = 0;
  /**
   * Computes a key for the texture that can be used to identify it in a cache. The key is written
   * to the provided BytesKey object.
   */
  virtual void computeTextureKey(Context* context, BytesKey* bytesKey) const = 0;

 protected:
  PixelFormat _format = PixelFormat::RGBA_8888;
  int _maxMipmapLevel = 0;

  GPUTexture(PixelFormat format, int maxMipmapLevel)
      : _format(format), _maxMipmapLevel(maxMipmapLevel) {
  }
};
}  // namespace tgfx
