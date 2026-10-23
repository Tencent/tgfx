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
struct PermutationMatchResult;

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
class PerlinNoiseFillShader : public PrecompiledShader {
 public:
  struct FragDims {
    enum : uint32_t { HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using D = FragDims;

  struct VertDims {
    enum : uint32_t { COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({});
    }
  };
  using VD = VertDims;

  // HAS_COVERAGE is mirrored: the vertex stage emits the coverage varying only when the fragment
  // stage consumes it, so vert and frag must agree. Enforced automatically by MirroredDimsAgree.
  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Runtime matching: does this draw belong to this family? Declared here, defined in
  /// the family's own .cpp, next to the GLSL it owns.
  static std::optional<PermutationMatchResult> Match(const ProgramInfo* programInfo);

  PrecompiledShaderInfo info() const override {
    return {"PerlinNoiseFillShader",
            "level1/perlin_noise.vert",
            "level1/perlin_noise.frag",
            VD::domain(),
            D::domain(),
            PermutationDomain({}),
            "",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::PerlinNoiseFillShader, tgfx::EnumeratePerlinNoiseFillReachable)
