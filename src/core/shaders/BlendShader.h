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
class BlendShader : public Shader {
 public:
  BlendShader(BlendMode mode, std::shared_ptr<Shader> dst, std::shared_ptr<Shader> src)
      : mode(mode), dst(std::move(dst)), src(std::move(src)) {
  }

  std::shared_ptr<Shader> makeWithMatrix(const Matrix& viewMatrix) const override;

  BlendMode mode;
  std::shared_ptr<Shader> dst;
  std::shared_ptr<Shader> src;

 protected:
  Type type() const override {
    return Type::Blend;
  }

  bool isEqual(const Shader* shader) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      const std::shared_ptr<ColorSpace>& dstColorSpace) const override;
};
}  // namespace tgfx
