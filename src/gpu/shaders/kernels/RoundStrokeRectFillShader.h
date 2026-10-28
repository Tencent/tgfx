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

class RoundStrokeRectFillShader : public ShaderFamily<RoundStrokeRectFillShader> {
 public:
  static constexpr const char* Name = "RoundStrokeRectFillShader";
  static constexpr const char* VertFile = "kernels/round_stroke_rect_fill.vert";
  static constexpr const char* FragFile = "kernels/round_stroke_rect_fill.frag";

  // The one geometry processor this family accepts: reported by info() and compared by Extract.
  static constexpr const char* GpClassName = "RoundStrokeRectGeometryProcessor";

  // The color attribute is unconditional (common-color batches broadcast the paint color; the
  // exact color rides the Color uniform through the HasCommonColor runtime uniform), so there is
  // no HAS_COMMON_COLOR dimension. The coverage and ellipse-radii attributes are unconditional
  // too (non-AA draws write a 1.0 coverage and zero radii), with the AA ramp selected by the
  // AAEnabled runtime uniform, so there is no HAS_AA dimension either.
  struct VertDims {
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationBoolDim<VertDims> HAS_UV_MATRIX{"HAS_UV_MATRIX"};
    static PermutationDomain domain() {
      return PermutationDomain({HAS_UV_MATRIX});
    }
  };
  using VD = VertDims;

  struct FragDims {
    // HAS_UV_MATRIX stays vertex-only: the fragment stage never branches on it
    // (round_stroke_rect_fill.frag does not reference the define), so it is not part of the
    // fragment domain.
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
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

  /// Matcher inputs: the UV-matrix presence and the XferProcessor kind.
  struct Inputs {
    bool hasUVMatrix = false;
    int xpType = XP_UNSUPPORTED;
  };

  /// Pure mapping from inputs to dimension values. Pass-through: every input value lands on the
  /// domain one-to-one, so the mirrored Reachable default enumerates exactly what Compose emits.
  static std::optional<RuleComposedValues> Compose(const Inputs& inputs) {
    if (inputs.xpType < XP_NONE) {
      return std::nullopt;
    }
    auto vert = ComposedDims<VertDims>(VertDims::domain());
    vert.set(VD::HAS_UV_MATRIX, inputs.hasUVMatrix ? 1 : 0);
    auto frag = ComposedDims<FragDims>(FragDims::domain());
    frag.set(FD::HAS_XP, inputs.xpType);
    RuleComposedValues values;
    values.vertValues = vert.finish();
    values.fragValues = frag.finish();
    return values;
  }

  /// Whole-draw guards (alpha-only target, geometry processor kind, perspective UV), defined in
  /// the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::RoundStrokeRectFillShader)
