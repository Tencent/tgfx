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
#include "gpu/GPUTexture.h"
#include "gpu/YUVFormat.h"
#include "gpu/resources/TextureView.h"
#include "tgfx/core/YUVColorSpace.h"

namespace tgfx {
/**
 * YUVTextureView wraps separate textures in the GPU backend for Y, U, and V planes.
 */
class YUVTextureView : public TextureView {
 public:
  /**
   * The pixel format of the texture view.
   */
  YUVFormat yuvFormat() const {
    return _yuvFormat;
  }

  /**
   * The color space of the texture view.
   */
  YUVColorSpace yuvColorSpace() const {
    return _colorSpace;
  }

  std::shared_ptr<GPUTexture> getTexture() const override {
    return textures.front();
  }

  /**
   * Returns the number of GPUTextures in the texture view.
   */
  size_t textureCount() const;

  /**
   * Returns the GPUTexture at the specified index.
   */
  std::shared_ptr<GPUTexture> getTextureAt(size_t index) const;

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

  void setColorSpace(std::shared_ptr<ColorSpace>) override {
  }

  std::shared_ptr<ColorSpace> colorSpace() const override;

 protected:
  YUVTextureView(std::vector<std::shared_ptr<GPUTexture>> yuvTextures, YUVFormat yuvFormat,
                 YUVColorSpace colorSpace);

 private:
  std::array<std::shared_ptr<GPUTexture>, 3> textures = {};
  YUVFormat _yuvFormat = YUVFormat::Unknown;
  YUVColorSpace _colorSpace = YUVColorSpace::BT601_LIMITED;

  friend class TextureView;
};
}  // namespace tgfx
