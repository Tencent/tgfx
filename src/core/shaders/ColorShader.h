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
class ColorShader : public Shader {
 public:
  explicit ColorShader(Color color) : color(color) {
  }

  bool isOpaque() const override;

  bool asColor(Color* color) const override;

  std::shared_ptr<Shader> makeWithMatrix(const Matrix&) const override {
    return weakThis.lock();
  }

  Color color;

 protected:
  Type type() const override {
    return Type::Color;
  }

  bool isEqual(const Shader* shader) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      std::shared_ptr<ColorSpace> colorSpace) const override;
};
}  // namespace tgfx
