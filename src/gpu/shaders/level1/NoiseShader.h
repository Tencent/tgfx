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

/// Precompiled shader declaration for PerlinNoiseFragmentProcessor. The fragment shader implements
/// the full Perlin noise algorithm with two LUT textures (permutations + gradient vectors). The
/// number of octaves, the noise type (FractalNoise vs Turbulence), and the stitch tile logic
/// are runtime uniforms, keeping the variant count bounded.
///
/// Vertex dimensions:
///   GP_TYPE (int, 2 values): 0=DefaultGeometryProcessor, 1=QuadPerEdgeAAGeometryProcessor
///
/// Fragment dimensions:
///   HAS_XP (int, 3 values): 0=EmptyXP, 1=PorterDuff DST_TEX, 2=PorterDuff FBF
///   HAS_COVERAGE (int, 3 values): 0=none, 1=AARectEffect, 2=AARectEffect+mask
///
/// Runtime uniforms: baseFrequency (float2), noiseType (float), numOctaves (float),
///                   stitchData (float2)
class NoiseShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { GP_TYPE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_TYPE", 2),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { HAS_XP, HAS_COVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
          PermutationInt("HAS_COVERAGE", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 2, "Update info() when fragment dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"NoiseShader",
            "level1/noise.vert",
            "level1/noise.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            ShouldCompile};
  }

 private:
  static bool ShouldCompile(uint32_t /*vertIndex*/, uint32_t /*fragIndex*/,
                            const std::vector<int>& /*vertValues*/,
                            const std::vector<int>& /*fragValues*/) {
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::NoiseShader)
