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

/// Cold "uber" shader for the (1 sampler, no CoverageFP, RGBA) structural class: samples one
/// intermediate texture then applies exactly one pointwise operator selected at runtime by the
/// OpType uniform (0=ColorMatrix, 1=Luma, 2=AlphaThreshold, 3=ColorSpaceXform). Replaces the four
/// former TexturedColorMatrix / TexturedLuma / TexturedAlphaThreshold / TexturedColorSpaceXform
/// shaders, which had an identical structural skeleton and differed only in the operator. OpType
/// and each operator's coefficients are set by the pointwise FP's onSetData (via setDataOptional),
/// so no compile-time operator dimension is needed — the four shaders collapse into one.
///
/// Used both by the composed-texture matcher (ComposeFragmentProcessor(TextureEffect, op) and the
/// equivalent sibling form) and, once wired, by the EffectDecomposer 2-pass pipeline.
class TexturedEffectShader : public PrecompiledShader {
 public:
  struct FragDims {
    // Subset clamping is runtime: the shader always declares Subset and clamps, and the writer
    // uploads the full texture bounds when there is no real subset, so the clamp is a no-op.
    enum : uint32_t { HAS_XP, HAS_COVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_COVERAGE"),
      });
    }
  };
  using D = FragDims;

  struct VertDims {
    enum : uint32_t { HAS_COVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
      });
    }
  };
  using VD = VertDims;

  // HAS_COVERAGE is mirrored: the vertex stage emits the coverage varying only when the fragment
  // stage consumes it, so vert and frag must agree. Enforced automatically by MirroredDimsAgree.
  PrecompiledShaderInfo info() const override {
    return {"TexturedEffectShader",
            "level1/textured_effect.vert",
            "level1/textured_effect.frag",
            VD::domain(),
            D::domain(),
            PermutationDomain({}),
            "",
            "",
            nullptr};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TexturedEffectShader)
