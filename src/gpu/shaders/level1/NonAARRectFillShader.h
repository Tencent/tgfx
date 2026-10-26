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

class NonAARRectFillShader : public ShaderFamily<NonAARRectFillShader> {
 public:
  static constexpr const char* Name = "NonAARRectFillShader";
  static constexpr const char* VertFile = "level1/non_aa_rrect_fill.vert";
  static constexpr const char* FragFile = "level1/non_aa_rrect_fill.frag";

  // The one geometry processor this family accepts: reported by info() and compared by Extract.
  static constexpr const char* GpClassName = "NonAARRectGeometryProcessor";

  TGFX_DEFINE_DIMS(STROKE, TEXTURED);
  using VD = Dims;

  struct FragDims {
    enum : uint32_t { STROKE, TEXTURED, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("STROKE"),
          PermutationBool("TEXTURED"),
          PermutationInt(XP_DEFINE_NAME, XP_VALUE_COUNT),
      });
    }
  };
  using FD = FragDims;
  static_assert(VD::COUNT == 2 && FD::COUNT == 3,
                "Update the Compose mapping when dimensions change.");

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
    return !(fragValues[FD::TEXTURED] != 0 && fragValues[FD::STROKE] != 0);
  }

  /// Pure mapping from inputs to dimension values, with the shared rejection above.
  static std::optional<RuleComposedValues> Compose(const Inputs& inputs) {
    if (inputs.xpType < XP_NONE) {
      return std::nullopt;
    }
    RuleComposedValues values;
    values.vertValues.resize(VD::COUNT);
    values.vertValues[VD::STROKE] = inputs.isStroke ? 1 : 0;
    values.vertValues[VD::TEXTURED] = inputs.textured ? 1 : 0;
    values.fragValues.resize(FD::COUNT);
    values.fragValues[FD::STROKE] = values.vertValues[VD::STROKE];
    values.fragValues[FD::TEXTURED] = values.vertValues[VD::TEXTURED];
    values.fragValues[FD::HAS_XP] = inputs.xpType;
    if (!FragValuesAllowed(values.fragValues)) {
      return std::nullopt;
    }
    return values;
  }

  /// Whole-draw guards (alpha-only target, geometry processor kind, the single-tap tiled texture
  /// shape), defined in the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::NonAARRectFillShader)
