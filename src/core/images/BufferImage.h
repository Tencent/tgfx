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
#include "PixelImage.h"

namespace tgfx {
/**
 * BufferImage wraps a fully decoded ImageBuffer that can generate textures on demand.
 */
class BufferImage : public PixelImage {
 public:
  BufferImage(std::shared_ptr<ImageBuffer> buffer, bool mipmapped);

  int width() const override {
    return imageBuffer->width();
  }

  int height() const override {
    return imageBuffer->height();
  }

  bool isAlphaOnly() const override {
    return imageBuffer->isAlphaOnly();
  }

  std::shared_ptr<ColorSpace> colorSpace() const override {
    return imageBuffer->colorSpace();
  }

  std::shared_ptr<ImageBuffer> imageBuffer = nullptr;

 protected:
  Type type() const override {
    return Type::Buffer;
  }

  float getRasterizedScale(float drawScale) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

  std::shared_ptr<Image> onMakeMipmapped(bool enabled) const override;

  std::shared_ptr<Image> onMakeScaled(int newWidth, int newHeight,
                                      const SamplingOptions& sampling) const override;
};
}  // namespace tgfx
