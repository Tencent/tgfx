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
  // The coverage attribute is unconditional (the non-AA path triangles carry a constant 1.0 slot
  // and the quad providers emit 1.0 for non-AA draws), so there is no HAS_COVERAGE dimension.
  struct FragDims {
    // Subset clamping is runtime: the shader always declares Subset and clamps, and the writer
    // uploads the full texture bounds when there is no real subset, so the clamp is a no-op.
    enum : uint32_t { HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt(XP_DEFINE_NAME, XP_VALUE_COUNT),
      });
    }
  };

  struct VertDims {
    enum : uint32_t { COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({});
    }
  };
  using FD = FragDims;
  using VD = VertDims;

  // HAS_COVERAGE is mirrored: the vertex stage emits the coverage varying only when the fragment
  // stage consumes it, so vert and frag must agree. Enforced automatically by MirroredDimsAgree.
  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Runtime matching: does this draw belong to this family? Declared here, defined in
  /// the family's own .cpp, next to the GLSL it owns.
  static std::optional<PermutationVariant> Match(const ProgramInfo* programInfo);

  PrecompiledShaderInfo info() const override {
    return {"TexturedEffectShader",
            "level1/textured_effect.vert",
            "level1/textured_effect.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TexturedEffectShader, tgfx::EnumerateTexturedEffectReachable)
