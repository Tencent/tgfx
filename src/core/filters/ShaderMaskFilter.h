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

#include <memory>
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Shader.h"

namespace tgfx {
class ShaderMaskFilter : public MaskFilter {
 public:
  ShaderMaskFilter(std::shared_ptr<Shader> shader, bool inverted)
      : shader(std::move(shader)), inverted(inverted) {
  }

  std::shared_ptr<Shader> getShader() const {
    return shader;
  }

  bool isInverted() const {
    return inverted;
  }

  std::shared_ptr<MaskFilter> makeWithMatrix(const Matrix& viewMatrix) const override;

 protected:
  Type type() const override {
    return Type::Shader;
  }

  bool isEqual(const MaskFilter* maskFilter) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                      const Matrix* uvMatrix) const override;

 private:
  std::shared_ptr<Shader> shader;
  bool inverted;
};
}  // namespace tgfx
