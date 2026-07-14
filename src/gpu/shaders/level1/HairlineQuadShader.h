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

class HairlineQuadShader : public PrecompiledShader {
 public:
  TGFX_DEFINE_DIMS(HAS_AA);
  using D = Dims;

  struct FragDims {
    enum : uint32_t { HAS_AA, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_AA"),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(D::COUNT == 1 && FD::COUNT == 2,
                "Update ShouldCompile below when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"HairlineQuadShader",
            "level1/hairline_quad.vert",
            "level1/hairline_quad.frag",
            D::domain(),
            FD::domain(),
            PermutationDomain({}),
            "HairlineQuadGeometryProcessor",
            "",
            ShouldCompile};
  }

 private:
  static bool ShouldCompile(uint32_t, uint32_t, const std::vector<int>& vertValues,
                            const std::vector<int>& fragValues) {
    return vertValues[0] == fragValues[0];
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::HairlineQuadShader)
