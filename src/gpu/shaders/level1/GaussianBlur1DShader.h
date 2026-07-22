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

/// Precompiled shader declaration for GaussianBlur1DFragmentProcessor. Performs a 1D Gaussian blur
/// by sampling a child texture in a loop. The loop upper bound is a fixed compile-time constant
/// (MAX_BLUR_SIGMA); the actual kernel radius is a runtime value derived from the Sigma uniform,
/// and the loop breaks early once it is reached. Sigma is therefore a runtime parameter, NOT a
/// compile-time permutation dimension — this keeps the variant count bounded (previously MAX_SIGMA
/// multiplied the whole fragment domain by 10).
///
/// Vertex dimensions:
///   GP_TYPE (int, 2 values): 0=DefaultGeometryProcessor, 1=QuadPerEdgeAAGeometryProcessor
///
/// Fragment dimensions:
///   (MAX_SIGMA removed — sigma is now a uniform, not a variant dimension)
///
/// Runtime uniforms: Sigma (float), Step (vec2 direction vector)
class GaussianBlur1DShader : public PrecompiledShader {
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
    enum : uint32_t { HAS_XP, HAS_CHILD_SUBSET, HAS_COVERAGE, HAS_TILED_CHILD, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_CHILD_SUBSET"),
          PermutationInt("HAS_COVERAGE", 3),
          PermutationBool("HAS_TILED_CHILD"),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 4, "Update info() when fragment dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"GaussianBlur1DShader",
            "level1/gaussian_blur_1d.vert",
            "level1/gaussian_blur_1d.frag",
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
                            const std::vector<int>& fragValues) {
    // A tiled child carries its own tiling coordinate handling, so the plain-child subset clamp is
    // never used at the same time; prune that redundant half of the cartesian product.
    if (fragValues[FD::HAS_TILED_CHILD] != 0 && fragValues[FD::HAS_CHILD_SUBSET] != 0) {
      return false;
    }
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::GaussianBlur1DShader)
