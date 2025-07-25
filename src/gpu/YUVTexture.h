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

#include <array>
#include "gpu/Texture.h"
#include "gpu/TextureSampler.h"
#include "gpu/YUVFormat.h"
#include "tgfx/core/YUVColorSpace.h"

namespace tgfx {
/**
 * YUVTexture wraps separate texture samplers in the GPU backend for Y, U, and V planes.
 */
class YUVTexture : public Texture {
 public:
  /**
   * The pixel format of this yuv texture.
   */
  YUVFormat yuvFormat() const {
    return _yuvFormat;
  }

  /**
   * The color space of the yuv texture.
   */
  YUVColorSpace colorSpace() const {
    return _colorSpace;
  }

  TextureSampler* getSampler() const override {
    return samplers.front().get();
  }

  /**
   * Returns the number of the samplers in the texture.
   */
  size_t samplerCount() const;

  /**
   * Returns a texture sampler at the specified index.
   */
  TextureSampler* getSamplerAt(size_t index) const;

  bool isAlphaOnly() const override {
    return false;
  }

  bool hasMipmaps() const override {
    return false;  // YUV textures do not support mipmaps.
  }

  bool isYUV() const final {
    return true;
  }

  size_t memoryUsage() const override;

  Point getTextureCoord(float x, float y) const override;

  BackendTexture getBackendTexture() const override {
    return {};
  }

 protected:
  YUVTexture(std::vector<std::unique_ptr<TextureSampler>> yuvSamplers, int width, int height,
             YUVFormat yuvFormat, YUVColorSpace colorSpace);

  void onReleaseGPU() override;

 private:
  std::array<std::unique_ptr<TextureSampler>, 3> samplers = {};
  YUVFormat _yuvFormat = YUVFormat::Unknown;
  YUVColorSpace _colorSpace = YUVColorSpace::BT601_LIMITED;

  friend class Texture;
};
}  // namespace tgfx
