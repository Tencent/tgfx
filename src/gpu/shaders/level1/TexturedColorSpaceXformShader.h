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

/// Precompiled shader for the EffectDecomposer 2-pass pipeline when the second FP is
/// ColorSpaceXformEffect. The intermediate texture from the first pass is sampled and then
/// transformed through the color space conversion pipeline.
///
/// Fragment dimensions:
///   HAS_SUBSET (bool): whether the intermediate texture needs subset clamping
///   UNPREMUL, LINEARIZE, SRC_OOTF, GAMUT_TRANSFORM, DST_OOTF, ENCODE, PREMUL (bool)
///
/// SRC_TF_TYPE and DST_TF_TYPE are runtime uniforms (SrcTFType / DstTFType) rather than
/// compile-time dimensions to reduce variant count.
class TexturedColorSpaceXformShader : public PrecompiledShader {
 public:
  struct FragDims {
    enum : uint32_t {
      HAS_SUBSET,
      UNPREMUL,
      LINEARIZE,
      SRC_OOTF,
      GAMUT_TRANSFORM,
      DST_OOTF,
      ENCODE,
      PREMUL,
      HAS_XP,
      COUNT
    };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_SUBSET"),
          PermutationBool("UNPREMUL"),
          PermutationBool("LINEARIZE"),
          PermutationBool("SRC_OOTF"),
          PermutationBool("GAMUT_TRANSFORM"),
          PermutationBool("DST_OOTF"),
          PermutationBool("ENCODE"),
          PermutationBool("PREMUL"),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 9, "Update ShouldCompile when fragment dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"TexturedColorSpaceXformShader",
            "level1/textured_color_space_xform.vert",
            "level1/textured_color_space_xform.frag",
            PermutationDomain({}),
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
    int unpremul = fragValues[FD::UNPREMUL];
    int linearize = fragValues[FD::LINEARIZE];
    int srcOotf = fragValues[FD::SRC_OOTF];
    int gamutTransform = fragValues[FD::GAMUT_TRANSFORM];
    int dstOotf = fragValues[FD::DST_OOTF];
    int encode = fragValues[FD::ENCODE];
    int premul = fragValues[FD::PREMUL];

    // All color-space flags zero is a no-op — never needed.
    if (!unpremul && !linearize && !srcOotf && !gamutTransform && !dstOotf && !encode && !premul) {
      return false;
    }
    // SRC_OOTF requires LINEARIZE=1 (HLGish source uses runtime SrcTFType check).
    if (srcOotf && !linearize) {
      return false;
    }
    // DST_OOTF requires ENCODE=1 (HLGinvish destination uses runtime DstTFType check).
    if (dstOotf && !encode) {
      return false;
    }
    // GAMUT_TRANSFORM requires at least LINEARIZE or ENCODE.
    if (gamutTransform && !linearize && !encode) {
      return false;
    }
    // UNPREMUL and PREMUL always appear together.
    if (unpremul != premul) {
      return false;
    }
    // If only UNPREMUL+PREMUL without any color transform, runtime short-circuits.
    if (unpremul && !linearize && !encode && !gamutTransform) {
      return false;
    }
    // Any nonlinear operation requires unpremul+premul wrapping.
    if ((linearize || encode) && !unpremul) {
      return false;
    }
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TexturedColorSpaceXformShader)
