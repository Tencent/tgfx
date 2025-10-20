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

#include "tgfx/core/Shader.h"

namespace tgfx {
class ColorFilterShader : public Shader {
 public:
  ColorFilterShader(std::shared_ptr<Shader> shader, std::shared_ptr<ColorFilter> colorFilter)
      : shader(std::move(shader)), colorFilter(std::move(colorFilter)) {
  }

  bool isOpaque() const override {
    return shader->isOpaque() && colorFilter->isAlphaUnchanged();
  }

  bool isAImage() const override {
    return shader->isAImage();
  }

  std::shared_ptr<Shader> makeWithMatrix(const Matrix& viewMatrix) const override;

  std::shared_ptr<Shader> shader;
  std::shared_ptr<ColorFilter> colorFilter;

 protected:
  Type type() const override {
    return Type::ColorFilter;
  }

  bool isEqual(const Shader* shader) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      std::shared_ptr<ColorSpace> dstColorSpace) const override;
};
}  // namespace tgfx
