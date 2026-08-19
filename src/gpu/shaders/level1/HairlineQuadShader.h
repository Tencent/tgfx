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
  struct FragDims {
    enum : uint32_t { HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 1, "Update the matcher below when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"HairlineQuadShader",
            "level1/hairline_quad.vert",
            "level1/hairline_quad.frag",
            PermutationDomain({}),
            FD::domain(),
            PermutationDomain({}),
            "HairlineQuadGeometryProcessor",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::HairlineQuadShader)
