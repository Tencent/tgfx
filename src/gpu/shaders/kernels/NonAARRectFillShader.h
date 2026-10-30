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

class FragmentProcessor;
class ProgramInfo;

class NonAARRectFillShader : public ShaderFamily<NonAARRectFillShader> {
 public:
  static constexpr const char* Name = "NonAARRectFillShader";
  static constexpr const char* VertFile = "kernels/non_aa_rrect_fill.vert";
  static constexpr const char* FragFile = "kernels/non_aa_rrect_fill.frag";

  // The one geometry processor this family accepts: reported by info() and compared by Extract.
  static constexpr const char* GpClassName = "NonAARRectGeometryProcessor";

  struct VertDims {
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationBoolDim<VertDims> STROKE{"STROKE"};
    static inline const PermutationBoolDim<VertDims> TEXTURED{"TEXTURED"};
    static PermutationDomain domain() {
      return PermutationDomain({STROKE, TEXTURED});
    }
  };
  using VD = VertDims;

  struct FragDims {
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationBoolDim<FragDims> STROKE{"STROKE"};
    static inline const PermutationBoolDim<FragDims> TEXTURED{"TEXTURED"};
    static inline const PermutationIntDim<FragDims> HAS_XP{XP_DEFINE_NAME, XP_VALUE_COUNT};
    static PermutationDomain domain() {
      return PermutationDomain({STROKE, TEXTURED, HAS_XP});
    }
  };
  using FD = FragDims;
  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Matcher inputs: the stroke and textured presence plus the XferProcessor kind.
  struct Inputs {
    bool isStroke = false;
    bool textured = false;
    int xpType = XP_UNSUPPORTED;
  };

  /// The textured variant (a TiledTextureEffect fill) is fill-only: a stroked rect never carries
  /// the tiled texture child, so the combination stays out of the bundle. Compose and the
  /// mirrored Reachable default both consult this predicate, keeping them in lockstep.
  static bool FragValuesAllowed(const std::vector<int>& fragValues) {
    // The signature stays vector-based: ShaderFamily's detection trait and the mirrored
    // reachable enumeration pass decoded (positional) values; the positions resolve by name.
    const auto fragDomain = FragDims::domain();
    return !(fragValues[fragDomain.indexOf(FD::TEXTURED.defineName)] != 0 &&
             fragValues[fragDomain.indexOf(FD::STROKE.defineName)] != 0);
  }

  /// Pure mapping from inputs to dimension values, with the shared rejection above.
  static std::optional<RuleComposedValues> Compose(const Inputs& inputs) {
    if (inputs.xpType < XP_NONE) {
      return std::nullopt;
    }
    int isStroke = inputs.isStroke ? 1 : 0;
    int textured = inputs.textured ? 1 : 0;
    auto vert = ComposedDims<VertDims>(VertDims::domain());
    vert.set(VD::STROKE, isStroke);
    vert.set(VD::TEXTURED, textured);
    auto frag = ComposedDims<FragDims>(FragDims::domain());
    frag.set(FD::STROKE, isStroke);
    frag.set(FD::TEXTURED, textured);
    frag.set(FD::HAS_XP, inputs.xpType);
    RuleComposedValues values;
    values.vertValues = vert.finish();
    values.fragValues = frag.finish();
    if (!FragValuesAllowed(values.fragValues)) {
      return std::nullopt;
    }
    return values;
  }

  /// Whole-draw guards (alpha-only target, geometry processor kind, the single-tap tiled texture
  /// shape), defined in the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo,
                                       PermutationMatchFailure* failure);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::NonAARRectFillShader)
