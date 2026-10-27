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

class ComplexEllipseFillShader : public ShaderFamily<ComplexEllipseFillShader> {
 public:
  static constexpr const char* Name = "ComplexEllipseFillShader";
  static constexpr const char* VertFile = "level1/complex_ellipse_fill.vert";
  static constexpr const char* FragFile = "level1/complex_ellipse_fill.frag";

  // The one geometry processor this family accepts: reported by info() and compared by Extract.
  static constexpr const char* GpClassName = "ComplexEllipseGeometryProcessor";

  struct VertDims {
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationBoolDim<VertDims> STROKE{"STROKE"};
    static PermutationDomain domain() {
      return PermutationDomain({STROKE});
    }
  };
  using VD = VertDims;

  struct FragDims {
    // The device-space mask is a runtime uniform (HasDeviceMask) with a padded dummy sampler: this
    // family is rare, and a HAS_DEVICE_MASK dimension would double all stroke/XP variants for it.
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationBoolDim<FragDims> STROKE{"STROKE"};
    static inline const PermutationIntDim<FragDims> HAS_XP{XP_DEFINE_NAME, XP_VALUE_COUNT};
    static PermutationDomain domain() {
      return PermutationDomain({STROKE, HAS_XP});
    }
  };
  using FD = FragDims;
  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Matcher inputs: the stroke presence and the XferProcessor kind.
  struct Inputs {
    bool isStroke = false;
    int xpType = XP_UNSUPPORTED;
  };

  /// Pure mapping from inputs to dimension values. Pass-through: every input value lands on the
  /// domains one-to-one (STROKE mirrors to both stages), so the mirrored Reachable default
  /// enumerates exactly what Compose emits.
  static std::optional<RuleComposedValues> Compose(const Inputs& inputs) {
    if (inputs.xpType < XP_NONE) {
      return std::nullopt;
    }
    int isStroke = inputs.isStroke ? 1 : 0;
    auto vert = ComposedDims<VertDims>(VertDims::domain());
    vert.set(VD::STROKE, isStroke);
    auto frag = ComposedDims<FragDims>(FragDims::domain());
    frag.set(FD::STROKE, isStroke);
    frag.set(FD::HAS_XP, inputs.xpType);
    RuleComposedValues values;
    values.vertValues = vert.finish();
    values.fragValues = frag.finish();
    return values;
  }

  /// Whole-draw guards (alpha-only target, geometry processor kind, the shared device-mask
  /// coverage value), defined in the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::ComplexEllipseFillShader)
