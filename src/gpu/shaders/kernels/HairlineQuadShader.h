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

class HairlineQuadShader : public ShaderFamily<HairlineQuadShader> {
 public:
  static constexpr const char* Name = "HairlineQuadShader";
  static constexpr const char* VertFile = "kernels/hairline_quad.vert";
  static constexpr const char* FragFile = "kernels/hairline_quad.frag";

  // The one geometry processor this family accepts: reported by info() and compared by Extract.
  static constexpr const char* GpClassName = "HairlineQuadGeometryProcessor";

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

  /// Whole-draw guards (alpha-only target, geometry processor kind, clip shape), defined in the
  /// family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::HairlineQuadShader)
