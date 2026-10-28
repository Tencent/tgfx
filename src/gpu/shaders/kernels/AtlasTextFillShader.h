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

class FragmentProcessor;
class ProgramInfo;

class AtlasTextFillShader : public ShaderFamily<AtlasTextFillShader> {
 public:
  static constexpr const char* Name = "AtlasTextFillShader";
  static constexpr const char* VertFile = "kernels/atlas_text_fill.vert";
  static constexpr const char* FragFile = "kernels/atlas_text_fill.frag";

  // The one geometry processor this family accepts: reported by info() and compared by Extract.
  static constexpr const char* GpClassName = "AtlasTextGeometryProcessor";

  struct VertDims {
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationBoolDim<VertDims> HAS_COVERAGE{"HAS_COVERAGE"};
    static PermutationDomain domain() {
      return PermutationDomain({HAS_COVERAGE});
    }
  };
  using VD = VertDims;

  struct FragDims {
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationBoolDim<FragDims> HAS_COVERAGE{"HAS_COVERAGE"};
    static inline const PermutationIntDim<FragDims> HAS_XP{XP_DEFINE_NAME, XP_VALUE_COUNT};
    static PermutationDomain domain() {
      return PermutationDomain({HAS_COVERAGE, HAS_XP});
    }
  };
  using FD = FragDims;
  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Resolves a draw's processor to the module Bind that takes over its uniform writes, or
  /// null to keep the processor's own onSetData.
  static ModuleBindFn BindingFor(const ProgramInfo* programInfo, const FragmentProcessor* fp);

  /// The PorterDuff XP module for PorterDuff draws, null otherwise.
  static XPBindFn XPBindingFor(const ProgramInfo* programInfo);

  /// Matcher inputs: the AA coverage presence and the XferProcessor kind.
  struct Inputs {
    bool hasCoverage = false;
    int xpType = XP_UNSUPPORTED;
  };

  /// Pure mapping from inputs to dimension values. Pass-through: every input value lands on the
  /// domains one-to-one (HAS_COVERAGE mirrors to both stages), so the mirrored Reachable default
  /// enumerates exactly what Compose emits.
  static std::optional<RuleComposedValues> Compose(const Inputs& inputs) {
    if (inputs.xpType < XP_NONE) {
      return std::nullopt;
    }
    int hasCoverage = inputs.hasCoverage ? 1 : 0;
    auto vert = ComposedDims<VertDims>(VertDims::domain());
    vert.set(VD::HAS_COVERAGE, hasCoverage);
    auto frag = ComposedDims<FragDims>(FragDims::domain());
    frag.set(FD::HAS_COVERAGE, hasCoverage);
    frag.set(FD::HAS_XP, inputs.xpType);
    RuleComposedValues values;
    values.vertValues = vert.finish();
    values.fragValues = frag.finish();
    return values;
  }

  /// Whole-draw guards (alpha-only target, geometry processor kind, the optional sole coverage
  /// FP), defined in the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::AtlasTextFillShader)
