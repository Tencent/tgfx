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

#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shader.h"

namespace tgfx {
class MatrixShader final : public Shader {
 public:
  static std::shared_ptr<Shader> MakeFrom(std::shared_ptr<Shader> source, const Matrix& viewMatrix);

  bool isOpaque() const override {
    return source->isOpaque();
  }

  bool isAImage() const override {
    return source->isAImage();
  }

  bool asColor(Color* color) const override {
    return source->asColor(color);
  }

  std::shared_ptr<Shader> makeWithMatrix(const Matrix& viewMatrix) const override;

  std::shared_ptr<Shader> source = nullptr;
  Matrix matrix = {};

 protected:
  Type type() const override {
    return Type::Matrix;
  }

  bool isEqual(const Shader* shader) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      std::shared_ptr<ColorSpace> colorSpace) const override;

  MatrixShader(std::shared_ptr<Shader> source, const Matrix& matrix);
};
}  // namespace tgfx
