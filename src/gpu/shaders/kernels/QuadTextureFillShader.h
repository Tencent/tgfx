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

class QuadTextureFillShader : public ShaderFamily<QuadTextureFillShader> {
 public:
  static constexpr const char* Name = "QuadTextureFillShader";
  static constexpr const char* VertFile = "kernels/quad_texture_fill.vert";
  static constexpr const char* FragFile = "kernels/quad_texture_fill.frag";

  // The one geometry processor this family accepts: reported by info() and compared by Extract.
  static constexpr const char* GpClassName = "QuadPerEdgeAAGeometryProcessor";

  struct VertDims {
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationBoolDim<VertDims> HAS_UV_COORD{"HAS_UV_COORD"};
    static inline const PermutationBoolDim<VertDims> HAS_SUBSET{"HAS_SUBSET"};
    static inline const PermutationBoolDim<VertDims> HAS_LOCAL_MASK{"HAS_LOCAL_MASK"};
    static PermutationDomain domain() {
      return PermutationDomain({HAS_UV_COORD, HAS_SUBSET, HAS_LOCAL_MASK});
    }
  };
  using VD = VertDims;

  struct FragDims {
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationBoolDim<FragDims> HAS_SUBSET{"HAS_SUBSET"};
    static inline const PermutationIntDim<FragDims> HAS_XP{XP_DEFINE_NAME, XP_VALUE_COUNT};
    static inline const PermutationBoolDim<FragDims> HAS_LOCAL_MASK{"HAS_LOCAL_MASK"};
    static PermutationDomain domain() {
      return PermutationDomain({HAS_SUBSET, HAS_XP, HAS_LOCAL_MASK});
    }
  };
  using FD = FragDims;
  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = true;

  /// Matcher inputs: the UV-coord and subset presence, the local-mask presence, and the
  /// XferProcessor kind.
  struct Inputs {
    bool hasUVCoord = false;
    bool hasSubset = false;
    bool hasLocalMask = false;
    int xpType = XP_UNSUPPORTED;
  };

  /// Pure mapping from inputs to dimension values. Pass-through: every input value lands on the
  /// domains one-to-one (HAS_SUBSET and HAS_LOCAL_MASK mirror to both stages), so the mirrored
  /// Reachable default enumerates exactly what Compose emits.
  static std::optional<RuleComposedValues> Compose(const Inputs& inputs) {
    if (inputs.xpType < XP_NONE) {
      return std::nullopt;
    }
    int hasSubset = inputs.hasSubset ? 1 : 0;
    int hasLocalMask = inputs.hasLocalMask ? 1 : 0;
    auto vert = ComposedDims<VertDims>(VertDims::domain());
    vert.set(VD::HAS_UV_COORD, inputs.hasUVCoord ? 1 : 0);
    vert.set(VD::HAS_SUBSET, hasSubset);
    vert.set(VD::HAS_LOCAL_MASK, hasLocalMask);
    auto frag = ComposedDims<FragDims>(FragDims::domain());
    // ALPHA_ONLY and HAS_RGBAAA are runtime uniforms (set by GLSLTextureEffect::onSetData), not
    // variants.
    frag.set(FD::HAS_SUBSET, hasSubset);
    frag.set(FD::HAS_XP, inputs.xpType);
    frag.set(FD::HAS_LOCAL_MASK, hasLocalMask);
    RuleComposedValues values;
    values.vertValues = vert.finish();
    values.fragValues = frag.finish();
    return values;
  }

  /// Whole-draw guards (alpha-only target, geometry processor kind, the color TextureEffect and
  /// coverage shapes), defined in the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo,
                                       PermutationMatchFailure* failure);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::QuadTextureFillShader)
