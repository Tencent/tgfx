/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "core/images/TransformImage.h"
#include "tgfx/core/Image.h"

namespace tgfx {
class ScaledImage : public TransformImage {
 public:
  ScaledImage(std::shared_ptr<Image> image, int width, int height, const SamplingOptions& sampling,
              bool mipmapped);

  ~ScaledImage() override = default;

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  bool hasMipmaps() const override {
    return mipmapped;
  }

 protected:
  Type type() const override {
    return Type::Scaled;
  }

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const SamplingArgs& samplingArgs, const Matrix* uvMatrix) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

  std::shared_ptr<Image> onMakeScaled(int newWidth, int newHeight,
                                      const SamplingOptions& sampling) const override;

  std::shared_ptr<Image> onMakeMipmapped(bool enabled) const override;

  std::shared_ptr<Image> onCloneWith(std::shared_ptr<Image> newSource) const override;

  std::optional<Matrix> concatUVMatrix(const Matrix* uvMatrix) const override;

 private:
  int _width = 0;
  int _height = 0;
  SamplingOptions sampling = {};
  bool mipmapped = false;
};
}  // namespace tgfx
