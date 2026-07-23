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
/// encode → premul.
///
/// The seven pipeline steps are selected at runtime through the CSFlags bitmask uniform
/// (mirrors ColorSpaceXformSteps::Flags::mask()) instead of compile-time dimensions, so a single
/// precompiled variant per HAS_XP covers every flag combination. SRC_TF_TYPE and DST_TF_TYPE are
/// likewise runtime uniforms (SrcTFType / DstTFType).
///
/// Fragment dimensions:
///   HAS_XP (int, 3): destination-read blend mode class for the fixed-function output stage.
class ColorSpaceXformShader : public PrecompiledShader {
 public:
  struct FragDims {
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
    return {"ColorSpaceXformShader",
            "level1/color_space_xform.vert",
            "level1/color_space_xform.frag",
            PermutationDomain({}),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            nullptr};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::ColorSpaceXformShader)
