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

/// Cold "uber" shader for the (1 sampler, no CoverageFP, RGBA) structural class: samples one
/// intermediate texture then applies exactly one pointwise operator selected at runtime by the
/// OpType uniform (0=ColorMatrix, 1=Luma, 2=AlphaThreshold, 3=ColorSpaceXform). Replaces the four
/// former TexturedColorMatrix / TexturedLuma / TexturedAlphaThreshold / TexturedColorSpaceXform
/// shaders, which had an identical structural skeleton and differed only in the operator. OpType
/// and each operator's coefficients are set by the pointwise FP's onSetData (via setDataOptional),
/// so no compile-time operator dimension is needed — the four shaders collapse into one.
///
/// Used both by the composed-texture matcher (ComposeFragmentProcessor(TextureEffect, op) and the
/// equivalent sibling form) and, once wired, by the EffectDecomposer 2-pass pipeline.
class TexturedEffectShader : public ShaderFamily<TexturedEffectShader> {
 public:
  static constexpr const char* Name = "TexturedEffectShader";
  static constexpr const char* VertFile = "kernels/textured_effect.vert";
  static constexpr const char* FragFile = "kernels/textured_effect.frag";

  // The coverage attribute is unconditional (the non-AA path triangles carry a constant 1.0 slot
  // and the quad providers emit 1.0 for non-AA draws), so there is no HAS_COVERAGE dimension.
  struct FragDims {
    // Subset clamping is runtime: the shader always declares Subset and clamps, and the writer
    // uploads the full texture bounds when there is no real subset, so the clamp is a no-op.
    // One typed handle per dimension; domain() lists them in the bundle's dimension order, and
    // Compose assigns through the handles by name (ComposedDims) — no index enum to keep
    // aligned with the domain list.
    static inline const PermutationIntDim<FragDims> HAS_XP{XP_DEFINE_NAME, XP_VALUE_COUNT};
    static PermutationDomain domain() {
      return PermutationDomain({HAS_XP});
    }
  };

  struct VertDims {
    // No vertex dimensions: an empty domain, with no handles to keep aligned.
    static PermutationDomain domain() {
      return PermutationDomain({});
    }
  };
  using FD = FragDims;
  using VD = VertDims;
  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Resolves a draw's processor to the module Bind that takes over its uniform writes, or
  /// null to keep the processor's own onSetData.
  static ModuleBindFn BindingFor(const ProgramInfo* programInfo, const FragmentProcessor* fp);

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

  /// Whole-draw guards (alpha-only target, position-only rect GP, the composed or sequential
  /// texture-plus-pointwise-operator shape), defined in the family's own .cpp, next to the GLSL
  /// it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::TexturedEffectShader)
