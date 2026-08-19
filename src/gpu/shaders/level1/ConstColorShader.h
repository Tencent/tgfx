/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

/// Precompiled shader declaration for ConstColorProcessor. Outputs a constant color uniform,
/// optionally modulated by the input color from the previous pipeline stage.
///
/// INPUT_MODE (0=Ignore, 1=ModulateRGBA, 2=ModulateA) is a runtime uniform (InputMode), written by
/// GLSLConstColorProcessor::onSetData, not a compile-time permutation.
class ConstColorShader : public PrecompiledShader {
 public:
  struct FragDims {
    enum : uint32_t { HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 1, "Update info() when fragment dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"ConstColorShader",
            "level1/const_color.vert",
            "level1/const_color.frag",
            PermutationDomain({}),
            FD::domain(),
            PermutationDomain({}),
            "",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::ConstColorShader)
