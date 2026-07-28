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

class SingleIntervalGradientShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { GP_TYPE, HAS_VCOVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_TYPE", 2),
          PermutationBool("HAS_VCOVERAGE"),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { GP_TYPE, HAS_XP, HAS_COVERAGE, HAS_VCOVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_TYPE", 2),
          PermutationInt("HAS_XP", 3),
          PermutationInt("HAS_COVERAGE", 3),
          PermutationBool("HAS_VCOVERAGE"),
      });
    }
  };
  using FD = FragDims;

  PrecompiledShaderInfo info() const override {
    return {"SingleIntervalGradientShader",
            "level1/gradient_fill.vert",
            "level1/single_interval_gradient.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            ShouldCompile};
  }

 private:
  // GP_TYPE and HAS_VCOVERAGE are mirrored across stages: the vertex shader emits the coverage
  // varying only when the fragment shader consumes it, and both must agree on the position transform.
  static bool ShouldCompile(uint32_t, uint32_t, const std::vector<int>& vertValues,
                            const std::vector<int>& fragValues) {
    return vertValues[VD::GP_TYPE] == fragValues[FD::GP_TYPE] &&
           vertValues[VD::HAS_VCOVERAGE] == fragValues[FD::HAS_VCOVERAGE];
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::SingleIntervalGradientShader)
