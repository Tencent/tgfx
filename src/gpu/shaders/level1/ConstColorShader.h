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
/// Fragment dimensions:
///   INPUT_MODE (int, 3 values): 0=Ignore, 1=ModulateRGBA, 2=ModulateA
class ConstColorShader : public PrecompiledShader {
 public:
  struct FragDims {
    enum : uint32_t { INPUT_MODE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("INPUT_MODE", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 1, "Update ShouldCompile when fragment dimensions change.");

  TGFX_DEFINE_DIMS(PLACEHOLDER_UNUSED);
  using VD = Dims;

  PrecompiledShaderInfo info() const override {
    // Vertex shader has no permutation dimensions — shares a single trivial pass-through.
    return {"ConstColorShader",
            "level1/const_color.vert",
            "level1/const_color.frag",
            PermutationDomain::FromBoolNames("PLACEHOLDER_UNUSED"),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            ShouldCompile};
  }

 private:
  static bool ShouldCompile(uint32_t vertIndex, uint32_t /*fragIndex*/,
                            const std::vector<int>& /*vertValues*/,
                            const std::vector<int>& /*fragValues*/) {
    // Only one vertex variant (index 0) is needed.
    return vertIndex == 0;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::ConstColorShader)
