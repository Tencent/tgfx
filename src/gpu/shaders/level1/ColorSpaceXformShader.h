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

/// Precompiled shader declaration for ColorSpaceXformEffect. Transforms colors through a
/// configurable pipeline of: unpremul → linearize → srcOOTF → gamutTransform → dstOOTF →
/// encode → premul. Each step is controlled by a boolean dimension.
///
/// Fragment dimensions:
///   UNPREMUL (bool): undo premultiplied alpha before processing
///   LINEARIZE (bool): apply source transfer function to convert to linear
///   SRC_OOTF (bool): apply source OOTF (HLG scenes)
///   GAMUT_TRANSFORM (bool): apply 3x3 color gamut matrix
///   DST_OOTF (bool): apply destination OOTF (HLG scenes)
///   ENCODE (bool): apply destination inverse transfer function
///   PREMUL (bool): re-premultiply alpha after processing
///   SRC_TF_TYPE (int, 4 values): source TF type (0=sRGBish, 1=PQish, 2=HLGish, 3=HLGinvish)
///   DST_TF_TYPE (int, 4 values): destination TF type
class ColorSpaceXformShader : public PrecompiledShader {
 public:
  struct FragDims {
    enum : uint32_t {
      UNPREMUL,
      LINEARIZE,
      SRC_OOTF,
      GAMUT_TRANSFORM,
      DST_OOTF,
      ENCODE,
      PREMUL,
      SRC_TF_TYPE,
      DST_TF_TYPE,
      HAS_XP,
      COUNT
    };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("UNPREMUL"),
          PermutationBool("LINEARIZE"),
          PermutationBool("SRC_OOTF"),
          PermutationBool("GAMUT_TRANSFORM"),
          PermutationBool("DST_OOTF"),
          PermutationBool("ENCODE"),
          PermutationBool("PREMUL"),
          PermutationInt("SRC_TF_TYPE", 4),
          PermutationInt("DST_TF_TYPE", 4),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 10, "Update ShouldCompile when fragment dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"ColorSpaceXformShader",
            "level1/color_space_xform.vert",
            "level1/color_space_xform.frag",
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
    int srcTfType = fragValues[FD::SRC_TF_TYPE];
    int dstTfType = fragValues[FD::DST_TF_TYPE];

    // All flags zero is a no-op transform — never needed as a shader.
    if (!unpremul && !linearize && !srcOotf && !gamutTransform && !dstOotf && !encode && !premul) {
      return false;
    }
    // SRC_TF_TYPE is only meaningful when LINEARIZE is active.
    if (!linearize && srcTfType != 0) {
      return false;
    }
    // DST_TF_TYPE is only meaningful when ENCODE is active.
    if (!encode && dstTfType != 0) {
      return false;
    }
    // SRC_OOTF requires LINEARIZE=1 and SRC_TF_TYPE=2 (HLGish source only).
    if (srcOotf && (!linearize || srcTfType != 2)) {
      return false;
    }
    // DST_OOTF requires ENCODE=1 and DST_TF_TYPE=3 (HLGinvish destination only).
    if (dstOotf && (!encode || dstTfType != 3)) {
      return false;
    }
    // SRC_TF_TYPE=3 (HLGinvish) never occurs on source side — it is an invert-only type.
    if (srcTfType == 3) {
      return false;
    }
    // DST_TF_TYPE=2 (HLGish) never occurs on destination side — HLG dst inverts to HLGinvish.
    if (dstTfType == 2) {
      return false;
    }
    // GAMUT_TRANSFORM requires at least LINEARIZE or ENCODE (gamut transform operates in linear
    // space; if both src and dst are already linear, the runtime skips the shader entirely).
    if (gamutTransform && !linearize && !encode) {
      return false;
    }
    // UNPREMUL and PREMUL always appear together (non-opaque) or are both absent (opaque).
    // The runtime optimizes away the asymmetric case.
    if (unpremul != premul) {
      return false;
    }
    // If only UNPREMUL+PREMUL without any color transform, runtime short-circuits (no shader).
    if (unpremul && !linearize && !encode && !gamutTransform) {
      return false;
    }
    // Any nonlinear operation (linearize/encode) requires unpremul+premul wrapping.
    if ((linearize || encode) && !unpremul) {
      return false;
    }
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::ColorSpaceXformShader)
