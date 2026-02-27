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

#include "gpu/resources/TextureView.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Shader.h"

namespace tgfx {
class ImageShader : public Shader {
 public:
  std::shared_ptr<Image> image = nullptr;
  TileMode tileModeX = TileMode::Clamp;
  TileMode tileModeY = TileMode::Clamp;
  SamplingOptions sampling = {};

  bool isAImage() const override {
    return true;
  }

 protected:
  Type type() const override {
    return Type::Image;
  }

  bool isEqual(const Shader* shader) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      const std::shared_ptr<ColorSpace>& dstColorSpace) const override;

 private:
  ImageShader(std::shared_ptr<Image> image, TileMode tileModeX, TileMode tileModeY,
              const SamplingOptions& sampling)
      : image(std::move(image)), tileModeX(tileModeX), tileModeY(tileModeY), sampling(sampling) {
  }

  friend class Shader;
};
}  // namespace tgfx
