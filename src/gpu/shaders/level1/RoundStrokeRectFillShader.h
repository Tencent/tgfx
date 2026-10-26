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

namespace tgfx {

class ProgramInfo;

class RoundStrokeRectFillShader : public ShaderFamily<RoundStrokeRectFillShader> {
 public:
  static constexpr const char* Name = "RoundStrokeRectFillShader";
  static constexpr const char* VertFile = "level1/round_stroke_rect_fill.vert";
  static constexpr const char* FragFile = "level1/round_stroke_rect_fill.frag";

  // The one geometry processor this family accepts: reported by info() and compared by Extract.
  static constexpr const char* GpClassName = "RoundStrokeRectGeometryProcessor";

  // The color attribute is unconditional (common-color batches broadcast the paint color; the
  // exact color rides the Color uniform through the HasCommonColor runtime uniform), so there is
  // no HAS_COMMON_COLOR dimension. The coverage and ellipse-radii attributes are unconditional
  // too (non-AA draws write a 1.0 coverage and zero radii), with the AA ramp selected by the
  // AAEnabled runtime uniform, so there is no HAS_AA dimension either.
  TGFX_DEFINE_DIMS(HAS_UV_MATRIX);
  using VD = Dims;

  struct FragDims {
    // HAS_UV_MATRIX stays vertex-only: the fragment stage never branches on it
    // (round_stroke_rect_fill.frag does not reference the define), so it is not part of the
    // fragment domain.
    enum : uint32_t { HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt(XP_DEFINE_NAME, XP_VALUE_COUNT),
      });
    }
  };
  using FD = FragDims;
  static_assert(VD::COUNT == 1 && FD::COUNT == 1,
                "Update the Compose mapping when dimensions change.");

  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

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
    RuleComposedValues values;
    values.vertValues.resize(VD::COUNT);
    values.vertValues[VD::HAS_UV_MATRIX] = inputs.hasUVMatrix ? 1 : 0;
    values.fragValues.resize(FD::COUNT);
    values.fragValues[FD::HAS_XP] = inputs.xpType;
    return values;
  }

  /// Whole-draw guards (alpha-only target, geometry processor kind, perspective UV), defined in
  /// the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::RoundStrokeRectFillShader)
