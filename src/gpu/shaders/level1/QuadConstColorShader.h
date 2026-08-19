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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/ShaderPermutation.h"

namespace tgfx {

/// Precompiled shader for QuadPerEdgeAAGeometryProcessor + ConstColorProcessor +
/// EmptyXferProcessor. InputMode is a runtime uniform, preserving all ConstColor input semantics.
class QuadConstColorShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { HAS_UV_COORD, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_UV_COORD"),
      });
    }
  };
  using VD = VertDims;
  static_assert(VD::COUNT == 1, "Update info() when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"QuadConstColorShader",
            "level1/quad_const_color.vert",
            "level1/quad_const_color.frag",
            VD::domain(),
            PermutationDomain({}),
            PermutationDomain({}),
            "QuadPerEdgeAAGeometryProcessor",
            "ConstColorProcessor"};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::QuadConstColorShader)
