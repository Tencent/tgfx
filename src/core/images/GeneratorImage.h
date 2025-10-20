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
 * GeneratorImage wraps an ImageGenerator that can generate ImageBuffers on demand.
 */
class GeneratorImage : public PixelImage {
 public:
  GeneratorImage(std::shared_ptr<ImageGenerator> generator, bool mipmapped);

  int width() const override {
    return generator->width();
  }

  int height() const override {
    return generator->height();
  }

  bool isAlphaOnly() const override {
    return generator->isAlphaOnly();
  }

  bool isFullyDecoded() const override {
    return false;
  }

  std::shared_ptr<ColorSpace> gamutColorSpace() const override {
    return generator->gamutColorSpace();
  }

 protected:
  Type type() const override {
    return Type::Generator;
  }

  std::shared_ptr<Image> onMakeDecoded(Context* context, bool tryHardware) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

  std::shared_ptr<Image> onMakeMipmapped(bool mipmapped) const override;

  std::shared_ptr<ImageGenerator> generator = nullptr;
};
}  // namespace tgfx
