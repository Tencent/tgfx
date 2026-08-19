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
//  License is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

/// Unified precompiled shader for ClampedGradientEffect with any of the four gradient colorizers
/// (single-interval, dual-interval, unrolled binary, LUT texture). The colorizer is a runtime
/// uniform (ColorizerKind), so the four legacy gradient kernels collapse into one; only the LUT
/// colorizer's sampler requires a compile-time dimension (HAS_LUT).
class UnifiedGradientShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { HAS_VCOVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_VCOVERAGE"),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { HAS_XP, HAS_VCOVERAGE, HAS_LUT, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_VCOVERAGE"),
          PermutationBool("HAS_LUT"),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 3, "Update the Compose mapping when fragment dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"UnifiedGradientShader",
            "level1/gradient_fill.vert",
            "level1/unified_gradient.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::UnifiedGradientShader)
