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

#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

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
/// HAS_COVERAGE is an independent structural dimension (mirrored vertex/fragment) driven by the
/// geometry processor's AAType, exactly like QuadTextureFillShader: when the GP carries a per-vertex
/// AA coverage attribute it is passed through as a varying and modulated into the output.
///
/// There is no GP_TYPE dimension: the vertex stage always multiplies by the Matrix uniform, which
/// DefaultGP fills with the view matrix and QuadPerEdgeAAGP fills with identity (bit-exact), so one
/// variant serves both.
///
/// Fragment dimensions:
///   HAS_XP (int, 3 values): XferProcessor type
///   HAS_COVERAGE (bool): per-vertex AA coverage varying present
///
/// Vertex dimensions:
///   HAS_COVERAGE (bool): per-vertex AA coverage attribute present
class TiledTextureFillShader : public PrecompiledShader {
 public:
  // Fragment dimensions
  //   TEXTURE_KIND (enum): the color texture's sampler type (0=TwoD, 1=Rect). Appended last so
  //   existing TwoD variant indices are unchanged. Rect variants compile only into the opengl
  //   bundle and never carry framebuffer fetch (HAS_XP=2), which is Vulkan/Metal-only.
  struct FragDims {
    enum : uint32_t { HAS_XP, HAS_COVERAGE, TEXTURE_KIND, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_COVERAGE"),
          PermutationEnum("TEXTURE_KIND", {"TwoD", "Rect"}),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 3, "Update info() when fragment dimensions change.");

  // Vertex dimensions
  struct VertDims {
    enum : uint32_t { HAS_COVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
      });
    }
  };
  using VD = VertDims;
  static_assert(VD::COUNT == 1, "Update info() when vertex dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"TiledTextureFillShader",
            "level1/tiled_texture_fill.vert",
            "level1/tiled_texture_fill.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            ShouldCompile};
  }

 private:
  static bool ShouldCompile(uint32_t, uint32_t, const std::vector<int>&,
                            const std::vector<int>& fragValues) {
    // Framebuffer fetch (HAS_XP=2) is Vulkan/Metal-only; Rect variants are opengl-only.
    return !(fragValues[FD::TEXTURE_KIND] == 1 && fragValues[FD::HAS_XP] == 2);
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TiledTextureFillShader)
