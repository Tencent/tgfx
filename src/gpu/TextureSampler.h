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

#include "gpu/YUVFormat.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/PixelFormat.h"
#include "tgfx/platform/HardwareBuffer.h"

namespace tgfx {
/**
 * The type of texture sampler. While only the 2D value is used by non-GL backends, the type must
 * still be known at the API-neutral layer to determine the legality of mipmapped, renderable, and
 * sampling parameters for proxies instantiated with wrapped textures.
 */
enum class SamplerType { None, TwoD, Rectangle, External };

/**
 * GPUTexture represents a texture in the GPU backend for rendering operations.
 */
class TextureSampler {
 public:
  /**
   * Returns the PixelFormat of the texture sampler created from the given hardware buffer. If the
   * hardwareBuffer is invalid or contains multiple planes (such as YUV formats), returns
   * PixelFormat::Unknown.
   */
  static PixelFormat GetPixelFormat(HardwareBufferRef hardwareBuffer);

  /**
   * Returns the PixelFormat of the backend texture. If the backendTexture is invalid, returns
   * PixelFormat::Unknown.
   */
  static PixelFormat GetPixelFormat(const BackendTexture& backendTexture);

  /**
   * Creates texture samplers from a platform-specific hardware buffer, such as AHardwareBuffer on
   * Android or CVPixelBufferRef on Apple platforms. The caller must ensure the hardwareBuffer stays
   * valid for the sampler's lifetime. Multiple samplers can be created from the same hardwareBuffer
   * (typically for YUV formats). If yuvFormat is not nullptr, it will be set to the
   * hardwareBuffer's YUVFormat. Returns nullptr if any parameter is invalid or the GPU backend does
   * not support the hardwareBuffer, leaving yuvFormat unchanged.
   */
  static std::vector<std::unique_ptr<TextureSampler>> MakeFrom(Context* context,
                                                               HardwareBufferRef hardwareBuffer,
                                                               YUVFormat* yuvFormat = nullptr);

  /**
   * Creates a new TextureSampler that wraps the given backend texture. If adopted is true, the
   * sampler will take ownership of the backend texture and destroy it when no longer needed.
   * Otherwise, the backend texture must remain valid for as long as the sampler exists.
   */
  static std::unique_ptr<TextureSampler> MakeFrom(Context* context,
                                                  const BackendTexture& backendTexture,
                                                  bool adopted = false);

  /**
   * Creates a new TextureSampler with the given width, height, and pixel format. If mipmapped is
   * true, mipmap levels will be generated. Returns nullptr if the sampler cannot be created.
   */
  static std::unique_ptr<TextureSampler> Make(Context* context, int width, int height,
                                              PixelFormat format = PixelFormat::RGBA_8888,
                                              bool mipmapped = false);

  virtual ~TextureSampler() = default;

  /**
   * Returns the pixel format of the sampler.
   */
  PixelFormat format() const {
    return _format;
  }

  /**
   * Returns the maximum mipmap level of the sampler.
   */
  int maxMipmapLevel() const {
    return _maxMipmapLevel;
  }

  /**
   * Returns true if the TextureSampler has mipmap levels.
   */
  bool hasMipmaps() const {
    return _maxMipmapLevel > 0;
  }

  /**
   * The texture type of the sampler.
   */
  virtual SamplerType type() const {
    return SamplerType::TwoD;
  }

  /**
   * Retrieves the backend texture with the specified size.
   */
  virtual BackendTexture getBackendTexture(int width, int height) const = 0;

  /**
   * Retrieves the backing hardware buffer. This method does not acquire any additional reference to
   * the returned hardware buffer. Returns nullptr if the sampler is not created from a hardware
   * buffer.
   */
  virtual HardwareBufferRef getHardwareBuffer() const {
    return nullptr;
  }

  /**
   * Writes pixel data to the sampler within the specified rectangle. The pixel data must match the
   * sampler's pixel format, and the rectangle must be fully contained within the sampler's
   * dimensions. If the sampler has mipmaps, you must call regenerateMipmapLevels() after writing
   * pixels; this will not happen automatically.
   */
  virtual void writePixels(Context* context, const Rect& rect, const void* pixels,
                           size_t rowBytes) = 0;
  /**
   * Computes a key for the sampler that can be used to identify it in a cache. The key is written
   * to the provided BytesKey object.
   */
  virtual void computeSamplerKey(Context* context, BytesKey* bytesKey) const = 0;

  /**
   * Releases the sampler and its GPU resources. Do not use the sampler after calling this method.
   * You must call this method explicitly, as GPU resources are not released automatically upon
   * destruction because a valid context may not be available at that time.
   */
  virtual void releaseGPU(Context* context) = 0;

 protected:
  PixelFormat _format = PixelFormat::RGBA_8888;
  int _maxMipmapLevel = 0;

  TextureSampler(PixelFormat format, int maxMipmapLevel)
      : _format(format), _maxMipmapLevel(maxMipmapLevel) {
  }
};
}  // namespace tgfx
