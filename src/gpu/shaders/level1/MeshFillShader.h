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

class MeshFillShader : public ShaderFamily<MeshFillShader> {
 public:
  static constexpr const char* Name = "MeshFillShader";
  static constexpr const char* VertFile = "level1/mesh_fill.vert";
  static constexpr const char* FragFile = "level1/mesh_fill.frag";

  // The one geometry processor this family accepts: reported by info() and compared by Extract.
  static constexpr const char* GpClassName = "MeshGeometryProcessor";

  TGFX_DEFINE_DIMS(HAS_TEX_COORDS);
  using VD = Dims;

  struct FragDims {
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

  /// Matcher inputs: the tex-coord presence and the XferProcessor kind.
  struct Inputs {
    bool hasTexCoords = false;
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
    values.vertValues[VD::HAS_TEX_COORDS] = inputs.hasTexCoords ? 1 : 0;
    values.fragValues.resize(FD::COUNT);
    values.fragValues[FD::HAS_XP] = inputs.xpType;
    return values;
  }

  /// Whole-draw guards (alpha-only target, geometry processor kind, fragment-processor count),
  /// defined in the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::MeshFillShader)
