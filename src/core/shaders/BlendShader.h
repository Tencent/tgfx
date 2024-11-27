/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

  ShaderType type() const override {
    return ShaderType::Blend;
  }

 protected:
  std::unique_ptr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                         const Matrix* uvMatrix) const override;

 private:
  BlendMode mode;
  std::shared_ptr<Shader> dst;
  std::shared_ptr<Shader> src;
};
}  // namespace tgfx
