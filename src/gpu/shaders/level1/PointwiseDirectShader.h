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

/// Precompiled shader declaration for a pointwise operator applied directly to the input color, with
/// no texture source. Serves LumaFragmentProcessor, AlphaThresholdFragmentProcessor and
/// ColorSpaceXformEffect: their skeletons were identical and differed only in the operator, which is
/// now the OpType runtime uniform instead of three separate shaders.
///
/// The operator kind and all of its parameters (luma coefficients, alpha threshold, color-space
/// steps) are runtime uniforms. Only the geometry-processor kind, the coverage varying and the
/// transfer-processor kind remain compile-time axes, because each changes the pipeline interface.
///
/// Vertex dimensions:
///   HAS_COVERAGE (bool): per-vertex AA coverage attribute present, driven by the GP's AAType and
///     so AA and non-AA draws of the same GP share one variant.
///
/// Fragment dimensions:
///   HAS_XP (int, 3 values): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF
///   HAS_COVERAGE (bool): mirrors the vertex dimension, controls the vCoverage varying input
class PointwiseDirectShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { HAS_COVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { HAS_XP, HAS_COVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_COVERAGE"),
      });
    }
  };
  using FD = FragDims;

  PrecompiledShaderInfo info() const override {
    return {"PointwiseDirectShader",
            "level1/pointwise_direct.vert",
            "level1/pointwise_direct.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            nullptr};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::PointwiseDirectShader)
