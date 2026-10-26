/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

class ProgramInfo;
struct PermutationVariant;

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
class TiledTextureFillShader : public PrecompiledShader {
 public:
  // Fragment dimensions
  //
  struct FragDims {
    enum : uint32_t { HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt(XP_DEFINE_NAME, XP_VALUE_COUNT),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 1, "Update info() when fragment dimensions change.");

  // Vertex dimensions
  struct VertDims {
    enum : uint32_t { COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({});
    }
  };
  using VD = VertDims;
  static_assert(VD::COUNT == 0, "Update info() when vertex dimensions change.");

  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Runtime matching: does this draw belong to this family? Declared here, defined in
  /// the family's own .cpp, next to the GLSL it owns.
  static std::optional<PermutationVariant> Match(const ProgramInfo* programInfo);

  PrecompiledShaderInfo info() const override {
    return {"TiledTextureFillShader",
            "level1/tiled_texture_fill.vert",
            "level1/tiled_texture_fill.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            ShaderTextureReadSwizzle};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TiledTextureFillShader, tgfx::EnumerateTiledTextureFillReachable)
