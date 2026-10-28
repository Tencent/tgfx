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

/// Precompiled shader for PerlinNoiseFragmentProcessor (SVG feTurbulence). Samples two per-draw
/// lookup textures (a 256x1 alpha-only permutations table and a 256x4 RGBA gradient table) and
/// accumulates octaves of gradient noise. The three parameters that the runtime path bakes into
/// generated code — noise type (fractal vs turbulence), octave count, and tile stitching — are all
/// folded into runtime uniforms here (NoiseType / NumOctaves / StitchTiles), so no per-parameter
/// permutation is needed: the octave loop runs a uniform number of iterations and the two variant
/// branches are uniform conditionals. This keeps the noise leaf to a handful of structural variants.
///
/// The coverage attribute is unconditional (the non-AA path triangles carry a constant 1.0 slot
/// and the quad providers emit 1.0 for non-AA draws), so there is no HAS_COVERAGE dimension.
class PerlinNoiseFillShader : public ShaderFamily<PerlinNoiseFillShader> {
 public:
  static constexpr const char* Name = "PerlinNoiseFillShader";
  static constexpr const char* VertFile = "kernels/perlin_noise.vert";
  static constexpr const char* FragFile = "kernels/perlin_noise.frag";

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

  struct VertDims {
    // No vertex dimensions: an empty domain, with no handles to keep aligned.
    static PermutationDomain domain() {
      return PermutationDomain({});
    }
  };
  using VD = VertDims;
  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// The PorterDuff XP module for PorterDuff draws, null otherwise.
  static XPBindFn XPBindingFor(const ProgramInfo* programInfo);

  /// Resolves a draw's processor to the module Bind that takes over its uniform writes, or
  /// null to keep the processor's own onSetData.
  static ModuleBindFn BindingFor(const ProgramInfo* programInfo, const FragmentProcessor* fp);

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

  /// Whole-draw guards (alpha-only target, position-only rect GP, the bare or composed pointwise
  /// operator shape), defined in the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::PerlinNoiseFillShader)
