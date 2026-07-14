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

class AtlasTextFillShader : public PrecompiledShader {
 public:
  TGFX_DEFINE_DIMS(HAS_COVERAGE, HAS_COMMON_COLOR, ALPHA_ONLY);
  using D = Dims;

  struct FragDims {
    enum : uint32_t { HAS_COVERAGE, HAS_COMMON_COLOR, ALPHA_ONLY, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_COMMON_COLOR"),
          PermutationBool("ALPHA_ONLY"),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(D::COUNT == 3 && FD::COUNT == 4,
                "Update ShouldCompile below when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"AtlasTextFillShader",
            "level1/atlas_text_fill.vert",
            "level1/atlas_text_fill.frag",
            D::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            ShouldCompile};
  }

 private:
  static bool ShouldCompile(uint32_t, uint32_t, const std::vector<int>& vertValues,
                            const std::vector<int>& fragValues) {
    // Both stages share the same domain, so only compile matching pairs.
    return vertValues[0] == fragValues[0] && vertValues[1] == fragValues[1] &&
           vertValues[2] == fragValues[2];
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::AtlasTextFillShader)
