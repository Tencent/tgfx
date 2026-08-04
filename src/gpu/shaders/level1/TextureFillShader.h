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

class TextureFillShader : public PrecompiledShader {
 public:
  // ALPHA_ONLY / HAS_RGBAAA are runtime uniforms (see frag), not variants. The vertex shader has
  // no structural axes: texture subset clamping is fragment-local and YUV falls back before
  // matching, so neither may produce duplicate vertex artifacts.
  struct VertDims {
    static PermutationDomain domain() {
      return PermutationDomain({});
    }
  };
  using VD = VertDims;

  // Fragment dimensions:
  //   HAS_COVERAGE (int, 3 values):
  //     0 = no coverage
  //     1 = AARectEffect only (rect clip via uniform)
  //     2 = AARectEffect + MaskTexture (DeviceSpaceTextureEffect mask + rect clip)
  // ALPHA_ONLY and HAS_RGBAAA are folded into runtime uniforms (AlphaOnly / HasRgbaaa), set by
  // GLSLTextureEffect::onSetData, rather than compile-time permutations.
  struct FragDims {
    enum : uint32_t { HAS_SUBSET, HAS_XP, HAS_COVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_SUBSET"),
          PermutationInt("HAS_XP", 3),
          PermutationInt("HAS_COVERAGE", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 3, "Update the encoders and ShouldCompile when dimensions change.");

  struct FragmentValues {
    bool hasSubset = false;
    uint32_t xp = 0;
    uint32_t coverage = 0;
  };

  /** Returns the stable template name used by both bundle generation and runtime lookup. */
  static constexpr const char* Name() {
    return "TextureFillShader";
  }

  /** TextureFill has one vertex artifact because its vertex interface is invariant. */
  static constexpr uint32_t EncodeVertex() {
    return 0;
  }

  /** Encodes fragment values with the same mixed-radix layout as FD::domain() without allocation. */
  static constexpr uint32_t EncodeFragment(const FragmentValues& values) {
    auto base = static_cast<uint32_t>(values.hasSubset);
    return base + values.xp * 2u + values.coverage * 6u;
  }

  PrecompiledShaderInfo info() const override {
    return {Name(),
            "level1/texture_fill.vert",
            "level1/texture_fill.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            ShouldCompile};
  }

 private:
  static bool ShouldCompile(uint32_t, uint32_t, const std::vector<int>&, const std::vector<int>&) {
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TextureFillShader)
