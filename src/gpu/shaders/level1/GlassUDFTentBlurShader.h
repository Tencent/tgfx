/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause/
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

/// Precompiled shader declaration for GlassUDFTentBlurFragmentProcessor. Blurs alpha coverage with
/// a tent kernel in one direction and writes one field into an RGBA8 target. The tent weights are
/// computed analytically inside the shader (linear kernel), matching the runtime emission
/// expression-for-expression, so no kernel table is uploaded. The loop upper bound is the fixed
/// compile-time constant 64 (the sole maxRadius every production call site passes); the actual
/// radius comes from the GlassUDFRadius uniform and the loop breaks early via the weight test.
///
/// The pass reads the radius component selected by the Field uniform (x = fine/Refraction,
/// y = coarse/EdgeLight) and the packed-input decode is gated by the InputIsPacked uniform, so a
/// single variant serves the horizontal (plain child, unpacked input) and vertical (tiled child,
/// packed input) passes of both fields.
///
/// Vertex dimensions:
///   (none — the Matrix uniform absorbs the GP difference, mirroring GaussianBlur1DShader)
///
/// Fragment dimensions:
///   HAS_XP (int, 3 values): XferProcessor type
///
/// Runtime uniforms: GlassUDFRadius (vec2 fine/coarse), GlassUDFStep (vec2 direction), Field (int),
/// InputIsPacked (int), Subset/ShaderModeX/ShaderModeY/Clamp/Dimension/TiledChild (shared child
/// contract, uploaded by the child processor's onSetData).
class GlassUDFTentBlurShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({});
    }
  };
  using VD = VertDims;

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
    return {"GlassUDFTentBlurShader",
            "level1/glass_udf_tent_blur.vert",
            "level1/glass_udf_tent_blur.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::GlassUDFTentBlurShader)
