/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include <vector>
#include "gpu/shaders/ShaderFamily.h"
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

class ProgramInfo;

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
class GlassUDFTentBlurShader : public ShaderFamily<GlassUDFTentBlurShader> {
 public:
  static constexpr const char* Name = "GlassUDFTentBlurShader";
  static constexpr const char* VertFile = "kernels/blur_texture.vert";
  static constexpr const char* FragFile = "kernels/glass_udf_tent_blur.frag";

  struct VertDims {
    // No vertex dimensions: an empty domain, with no handles to keep aligned.
    static PermutationDomain domain() {
      return PermutationDomain({});
    }
  };
  using VD = VertDims;

  struct FragDims {
    // One typed handle per dimension; domain() lists them in the bundle's dimension order, and
    // Compose assigns through the handles by name (ComposedDims) — no index enum to keep
    // aligned with the domain list.
    static inline const PermutationIntDim<FragDims> HAS_XP{XP_DEFINE_NAME, XP_VALUE_COUNT};
    static PermutationDomain domain() {
      return PermutationDomain({HAS_XP});
    }
  };
  using FD = FragDims;
  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// The PorterDuff XP module for PorterDuff draws, null otherwise.
  static XPBindFn XPBindingFor(const ProgramInfo* programInfo);

  /// Matcher inputs: only the XferProcessor kind shapes the variant.
  struct Inputs {
    int xpType = XP_UNSUPPORTED;
  };

  /// Pure mapping from inputs to dimension values. Pass-through: every input value lands on the
  /// domain one-to-one, so the mirrored Reachable default enumerates exactly what Compose emits.
  static std::optional<RuleComposedValues> Compose(const Inputs& inputs) {
    if (inputs.xpType < XP_NONE) {
      return std::nullopt;
    }
    auto frag = ComposedDims<FragDims>(FragDims::domain());
    frag.set(FD::HAS_XP, inputs.xpType);
    RuleComposedValues values;
    values.fragValues = frag.finish();
    return values;
  }

  /// Whole-draw guards (alpha-only target, GP kind, the blur child shape and the fixed radius
  /// budget), defined in the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::GlassUDFTentBlurShader)
