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
/// (MAX_BLUR_SIGMA); the actual kernel radius is a runtime value taken from the Radius uniform,
/// and the loop breaks early once it is reached. The half-kernel weights are precomputed and
/// normalized on the CPU and uploaded via the shared Kernel vec4 array, matching the runtime
/// path's uniform contract bit-for-bit. The radius is therefore a runtime parameter, NOT a
/// compile-time permutation dimension — this keeps the variant count bounded (previously MAX_SIGMA
/// multiplied the whole fragment domain by 10).
///
/// Vertex dimensions:
///
/// Fragment dimensions:
///   (MAX_SIGMA removed — the radius is now a uniform, not a variant dimension)
///
/// Runtime uniforms: Kernel (vec4[KERNEL_VEC4_COUNT]), Radius (int), Step (vec2 direction vector)
class GaussianBlur1DShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({});
    }
  };
  using VD = VertDims;

  struct FragDims {
    // Child subset clamping is runtime: the shader always declares Subset and every tap clamps,
    // and a child without a real subset uploads the full texture bounds, so the clamp is a no-op.
    // The device mask and the tiled-child tap path are runtime uniforms (HasDeviceMask /
    // TiledChild), not permutation dimensions.
    enum : uint32_t { HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 1, "Update info() when fragment dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"GaussianBlur1DShader",
            "level1/gaussian_blur_1d.vert",
            "level1/gaussian_blur_1d.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::GaussianBlur1DShader)
