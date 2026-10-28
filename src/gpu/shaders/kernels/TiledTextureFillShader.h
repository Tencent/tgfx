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

/// Precompiled shader declaration for TiledTextureEffect. Handles texture sampling with various
/// tiling modes (repeat, mirror, clamp-to-border, etc.) applied per-axis via runtime uniform
/// branches. ShaderModeX/ShaderModeY are passed as uniforms rather than compile-time permutations
/// to dramatically reduce variant count.
///
/// ALPHA_ONLY (alpha-only texture) and HAS_STRICT (SrcRectConstraint::Strict) are pure fragment
/// math, so they are folded into runtime uniforms (AlphaOnly / Strict) rather than compile-time
/// permutations, mirroring the QuadTextureFillShader approach. Perspective is never matched here
/// (the matcher rejects TiledTextureEffect::hasPerspective), so no perspective vertex dimension
/// exists: the coordinate is always affine (vec2).
///
/// The coverage attribute is unconditional (the non-AA path triangles carry a constant 1.0 slot
/// and the quad providers emit 1.0 for non-AA draws), so there is no HAS_COVERAGE dimension.
///
/// There is no GP_TYPE dimension: the vertex stage always multiplies by the Matrix uniform, which
/// DefaultGP fills with the view matrix and QuadPerEdgeAAGP fills with identity (bit-exact), so one
/// variant serves both.
///
/// Fragment dimensions:
///   HAS_XP (int, 3 values): XferProcessor type
class TiledTextureFillShader : public ShaderFamily<TiledTextureFillShader> {
 public:
  static constexpr const char* Name = "TiledTextureFillShader";
  static constexpr const char* VertFile = "kernels/tiled_texture_fill.vert";
  static constexpr const char* FragFile = "kernels/tiled_texture_fill.frag";

  // The kernel implements the GRAY_8 / RG_88 runtime read swizzle (texture_read_swizzle.inc), so
  // the matcher may route such texture reads here instead of refusing the draw.
  static constexpr uint32_t TextureReads = ShaderTextureReadSwizzle;

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

  /// Whole-draw guards (alpha-only target, position-only rect GP, the servable tiling-mode
  /// shape), defined in the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::TiledTextureFillShader)
