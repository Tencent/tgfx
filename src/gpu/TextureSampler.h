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
 * TextureSampler stores the sampling parameters for a backend texture uint.
 */
class TextureSampler {
 public:
  /**
   * Retrieves the pixel formats of texture samplers created from the specified hardware buffer. If
   * yuvFormat is not nullptr and the hardware buffer uses a YUV format, it will be set to that
   * format; otherwise, it will be set to YUVFormat::Unknown.
   */
  static std::vector<PixelFormat> GetFormats(HardwareBufferRef hardwareBuffer,
                                             YUVFormat* yuvFormat = nullptr);

  /**
   * Creates a new TextureSampler which wraps the specified backend texture. The caller is
   * responsible for managing the lifetime of the backendTexture.
   */
  static std::unique_ptr<TextureSampler> MakeFrom(Context* context,
                                                  const BackendTexture& backendTexture);

  virtual ~TextureSampler() = default;

  /**
   * The pixel format of the sampler.
   */
  PixelFormat format = PixelFormat::RGBA_8888;

  /**
   * The maximum mipmap level of the sampler.
   */
  int maxMipmapLevel = 0;

  /**
   * The texture type of the sampler.
   */
  virtual SamplerType type() const {
    return SamplerType::TwoD;
  }

  /**
   * Returns true if the TextureSampler has mipmap levels.
   */
  bool hasMipmaps() const {
    return maxMipmapLevel > 0;
  }

  /**
   * Retrieves the backend texture with the specified size.
   */
  virtual BackendTexture getBackendTexture(int width, int height) const = 0;

 protected:
  virtual void computeKey(Context* context, BytesKey* bytesKey) const = 0;

  friend class FragmentProcessor;
  friend class Pipeline;
};
}  // namespace tgfx
