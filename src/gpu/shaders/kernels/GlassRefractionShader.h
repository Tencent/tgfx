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

class GlassRefractionShader : public ShaderFamily<GlassRefractionShader> {
 public:
  static constexpr const char* Name = "GlassRefractionShader";
  static constexpr const char* VertFile = "kernels/glass_refraction.vert";
  static constexpr const char* FragFile = "kernels/glass_refraction.frag";

  struct VertDims {
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationIntDim<VertDims> GP_KIND{"GP_KIND", 2};
    static PermutationDomain domain() {
      return PermutationDomain({GP_KIND});
    }
  };
  using VD = VertDims;

  struct FragDims {
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationIntDim<FragDims> GP_KIND{"GP_KIND", 2};
    static inline const PermutationIntDim<FragDims> GEOMETRY_KIND{"GEOMETRY_KIND", 3};
    static inline const PermutationIntDim<FragDims> DISPERSION_ON{"DISPERSION_ON", 2};
    static inline const PermutationIntDim<FragDims> HAS_XP{XP_DEFINE_NAME, XP_VALUE_COUNT};
    static PermutationDomain domain() {
      return PermutationDomain({GP_KIND, GEOMETRY_KIND, DISPERSION_ON, HAS_XP});
    }
  };
  using FD = FragDims;
  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Matcher inputs: the GP kind (ellipse vs uv-matrix quad), the geometry child kind (SDF
  /// rounded-rect / SDF other / UDF), the dispersion flag, and the XferProcessor kind.
  struct Inputs {
    int gpKind = -1;
    int geometryKind = -1;
    bool dispersionOn = false;
    int xpType = XP_UNSUPPORTED;
  };

  /// Pure mapping from inputs to dimension values. Pass-through: every input value lands on the
  /// domains one-to-one (GP_KIND mirrors to both stages), so the mirrored Reachable default
  /// enumerates exactly what Compose emits.
  static std::optional<RuleComposedValues> Compose(const Inputs& inputs) {
    if (inputs.gpKind < 0 || inputs.geometryKind < 0 || inputs.xpType < XP_NONE) {
      return std::nullopt;
    }
    auto vert = ComposedDims<VertDims>(VertDims::domain());
    vert.set(VD::GP_KIND, inputs.gpKind);
    auto frag = ComposedDims<FragDims>(FragDims::domain());
    frag.set(FD::GP_KIND, inputs.gpKind);
    frag.set(FD::GEOMETRY_KIND, inputs.geometryKind);
    frag.set(FD::DISPERSION_ON, inputs.dispersionOn ? 1 : 0);
    frag.set(FD::HAS_XP, inputs.xpType);
    RuleComposedValues values;
    values.vertValues = vert.finish();
    values.fragValues = frag.finish();
    return values;
  }

  /// Whole-draw guards (alpha-only target, the common-color GP forms, the local-mask coverage,
  /// the refraction and geometry child shapes), defined in the family's own .cpp, next to the
  /// GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::GlassRefractionShader)
