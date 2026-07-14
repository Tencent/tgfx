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
/// by sampling a child texture in a loop. The loop upper bound is determined at compile time by
/// MAX_SIGMA, which controls the kernel radius (loop count = 4 * MAX_SIGMA + 1).
///
/// Vertex dimensions:
///   GP_TYPE (int, 2 values): 0=DefaultGeometryProcessor, 1=QuadPerEdgeAAGeometryProcessor
///
/// Fragment dimensions:
///   MAX_SIGMA (int, 10 values): maps to maxSigma 1~10 (index 0 → maxSigma=1, index 9 → maxSigma=10)
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
    enum : uint32_t { MAX_SIGMA, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("MAX_SIGMA", 10),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 2, "Update info() when fragment dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"GaussianBlur1DShader",
            "level1/gaussian_blur_1d.vert",
            "level1/gaussian_blur_1d.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            nullptr};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::GaussianBlur1DShader)
