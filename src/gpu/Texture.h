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

#include "gpu/Resource.h"
#include "gpu/TextureSampler.h"
#include "tgfx/core/ImageBuffer.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/YUVColorSpace.h"
#include "tgfx/core/YUVData.h"
#include "tgfx/gpu/ImageOrigin.h"
#include "tgfx/platform/HardwareBuffer.h"

namespace tgfx {
class BackendTexture;
class RenderTarget;

/**
 * Texture describes a two-dimensional array of pixels in the GPU backend for drawing.
 */
class Texture : public Resource {
 public:
  /**
   * Returns true if the specified texture size and format can be created by the GPU backend.
   */
  static bool CheckSizeAndFormat(Context* context, int width, int height, PixelFormat format);

  /**
   * Creates a new texture from the specified pixel data with each pixel stored as 32-bit RGBA
   * data. Returns nullptr if any of the parameters is invalid.
   */
  static std::shared_ptr<Texture> MakeRGBA(Context* context, int width, int height,
                                           const void* pixels, size_t rowBytes,
                                           bool mipmapped = false,
                                           ImageOrigin origin = ImageOrigin::TopLeft) {
    return MakeFormat(context, width, height, pixels, rowBytes, PixelFormat::RGBA_8888, mipmapped,
                      origin);
  }
  /**
   * Creates an empty texture with each pixel stored as 32-bit RGBA data. Returns nullptr if any of
   * the parameters is invalid.
   */
  static std::shared_ptr<Texture> MakeRGBA(Context* context, int width, int height,
                                           bool mipmapped = false,
                                           ImageOrigin origin = ImageOrigin::TopLeft) {
    return MakeFormat(context, width, height, nullptr, 0, PixelFormat::RGBA_8888, mipmapped,
                      origin);
  }

  /**
   * Creates a new texture from the specified pixel data with each pixel stored as a single
   * translucency (alpha) channel. Returns nullptr if any of the parameters is invalid or the
   * backend does not support creating alpha only textures.
   */
  static std::shared_ptr<Texture> MakeAlpha(Context* context, int width, int height,
                                            const void* pixels, size_t rowBytes,
                                            bool mipmapped = false,
                                            ImageOrigin origin = ImageOrigin::TopLeft) {
    return MakeFormat(context, width, height, pixels, rowBytes, PixelFormat::ALPHA_8, mipmapped,
                      origin);
  }
  /**
   * Creates an empty texture with each pixel stored as a single translucency (alpha) channel.
   * Returns nullptr if any of the parameters is invalid or the backend does not support creating
   * alpha only textures.
   */
  static std::shared_ptr<Texture> MakeAlpha(Context* context, int width, int height,
                                            bool mipmapped = false,
                                            ImageOrigin origin = ImageOrigin::TopLeft) {
    return MakeFormat(context, width, height, nullptr, 0, PixelFormat::ALPHA_8, mipmapped, origin);
  }

  /**
   * Creates an empty texture with each pixel store as the pixelFormat describes. Returns nullptr if
   * any of the parameters is invalid or the backend does not support creating textures of the
   * specified pixelFormat.
   */
  static std::shared_ptr<Texture> MakeFormat(Context* context, int width, int height,
                                             PixelFormat pixelFormat, bool mipmapped = false,
                                             ImageOrigin origin = ImageOrigin::TopLeft) {
    return MakeFormat(context, width, height, nullptr, 0, pixelFormat, mipmapped, origin);
  }

  /**
   * Creates a new texture from the specified pixel data with each pixel store as the pixelFormat
   * describes. Returns nullptr if any of the parameters is invalid or the backend does not support
   * creating textures of the specified pixelFormat.
   */
  static std::shared_ptr<Texture> MakeFormat(Context* context, int width, int height,
                                             const void* pixels, size_t rowBytes,
                                             PixelFormat pixelFormat, bool mipmapped = false,
                                             ImageOrigin origin = ImageOrigin::TopLeft);

  /**
   * Creates a new Texture which wraps the specified backend texture. The caller must ensure the
   * backend texture is valid for the lifetime of returned Texture.
   */
  static std::shared_ptr<Texture> MakeFrom(Context* context, const BackendTexture& backendTexture,
                                           ImageOrigin origin = ImageOrigin::TopLeft,
                                           bool adopted = false);

  /**
   * Creates a Texture from the specified ImageBuffer. Returns nullptr if the context is nullptr or
   * the imageBuffer is nullptr.
   */
  static std::shared_ptr<Texture> MakeFrom(Context* context,
                                           std::shared_ptr<ImageBuffer> imageBuffer,
                                           bool mipmapped = false);

  /**
   * Creates a Texture from the platform-specific hardware buffer. For example, the hardware
   * buffer could be an AHardwareBuffer on the android platform or a CVPixelBufferRef on the
   * apple platform. The returned Texture takes a reference to the hardwareBuffer. The colorSpace
   * is ignored if the hardwareBuffer contains only one plane, indicating that it is not in the YUV
   * format. Returns nullptr if the context is nullptr or the hardwareBuffer is nullptr.
   */
  static std::shared_ptr<Texture> MakeFrom(Context* context, HardwareBufferRef hardwareBuffer,
                                           YUVColorSpace colorSpace = YUVColorSpace::BT601_LIMITED);

  /**
   * Creates a new Texture in the I420 format from the specified YUVData and YUVColorSpace. Returns
   * nullptr if the context is nullptr or the yuvData is invalid;
   */
  static std::shared_ptr<Texture> MakeI420(Context* context, const YUVData* yuvData,
                                           YUVColorSpace colorSpace = YUVColorSpace::BT601_LIMITED);

  /**
   * Creates a new Texture in the NV12 format from the specified YUVData and YUVColorSpace. Returns
   * nullptr if the context is nullptr or the yuvData is invalid;
   */
  static std::shared_ptr<Texture> MakeNV12(Context* context, const YUVData* yuvData,
                                           YUVColorSpace colorSpace = YUVColorSpace::BT601_LIMITED);

  /**
   * Returns the display width of this texture.
   */
  virtual int width() const {
    return _width;
  }

  /**
   * Returns the display height of this texture.
   */
  virtual int height() const {
    return _height;
  }

  /**
   * Returns the origin of the texture, either ImageOrigin::TopLeft or ImageOrigin::BottomLeft.
   */
  virtual ImageOrigin origin() const {
    return _origin;
  }

  /**
   * Returns true if pixels represent transparency only.
   */
  virtual bool isAlphaOnly() const {
    return getSampler()->format() == PixelFormat::ALPHA_8;
  }

  /**
   * Returns true if the texture has mipmap levels.
   */
  virtual bool hasMipmaps() const {
    return getSampler()->hasMipmaps();
  }

  /**
   * Returns true if this is a YUVTexture.
   */
  virtual bool isYUV() const {
    return false;
  }

  /**
   * Returns the default texture sampler.
   */
  virtual TextureSampler* getSampler() const = 0;

  /**
   * Returns the texture coordinates in backend units corresponding to specified position in pixels.
   */
  virtual Point getTextureCoord(float x, float y) const;

  /**
   * Retrieves the backend texture. Returns an invalid BackendTexture if the texture is a
   * YUVTexture.
   */
  virtual BackendTexture getBackendTexture() const {
    return getSampler()->getBackendTexture(_width, _height);
  }

  /**
   * Returns the underlying render target if this texture is also a render target; otherwise, returns nullptr.
   */
  virtual std::shared_ptr<RenderTarget> asRenderTarget() const {
    return nullptr;
  }

 protected:
  int _width = 0;
  int _height = 0;
  ImageOrigin _origin = ImageOrigin::TopLeft;

  Texture(int width, int height, ImageOrigin origin)
      : _width(width), _height(height), _origin(origin) {
  }
};
}  // namespace tgfx
