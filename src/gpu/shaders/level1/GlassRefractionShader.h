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
  static constexpr const char* VertFile = "level1/glass_refraction.vert";
  static constexpr const char* FragFile = "level1/glass_refraction.frag";

  struct VertDims {
    enum : uint32_t { GP_KIND, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_KIND", 2),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { GP_KIND, GEOMETRY_KIND, DISPERSION_ON, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_KIND", 2),
          PermutationInt("GEOMETRY_KIND", 3),
          PermutationInt("DISPERSION_ON", 2),
          PermutationInt(XP_DEFINE_NAME, XP_VALUE_COUNT),
      });
    }
  };
  using FD = FragDims;
  static_assert(VD::COUNT == 1 && FD::COUNT == 4,
                "Update the Compose mapping when dimensions change.");

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
    RuleComposedValues values;
    values.fragValues.resize(FD::COUNT);
    values.fragValues[FD::GP_KIND] = inputs.gpKind;
    values.fragValues[FD::GEOMETRY_KIND] = inputs.geometryKind;
    values.fragValues[FD::DISPERSION_ON] = inputs.dispersionOn ? 1 : 0;
    values.fragValues[FD::HAS_XP] = inputs.xpType;
    values.vertValues.resize(VD::COUNT);
    values.vertValues[VD::GP_KIND] = inputs.gpKind;
    return values;
  }

  /// Whole-draw guards (alpha-only target, the common-color GP forms, the local-mask coverage,
  /// the refraction and geometry child shapes), defined in the family's own .cpp, next to the
  /// GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::GlassRefractionShader)
