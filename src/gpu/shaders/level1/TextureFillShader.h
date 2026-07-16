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
  // Vertex dimensions (unchanged — XP does not affect vertex stage)
  TGFX_DEFINE_DIMS(HAS_YUV, ALPHA_ONLY, HAS_RGBAAA, HAS_SUBSET);
  using VD = Dims;

  // Fragment dimensions:
  //   HAS_COVERAGE (int, 3 values):
  //     0 = no coverage
  //     1 = AARectEffect only (rect clip via uniform)
  //     2 = AARectEffect + MaskTexture (DeviceSpaceTextureEffect mask + rect clip)
  struct FragDims {
    enum : uint32_t { HAS_YUV, ALPHA_ONLY, HAS_RGBAAA, HAS_SUBSET, HAS_XP, HAS_COVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_YUV"),
          PermutationBool("ALPHA_ONLY"),
          PermutationBool("HAS_RGBAAA"),
          PermutationBool("HAS_SUBSET"),
          PermutationInt("HAS_XP", 3),
          PermutationInt("HAS_COVERAGE", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 6, "Update ShouldCompile below when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"TextureFillShader",
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
  static bool ShouldCompile(uint32_t, uint32_t, const std::vector<int>& vertValues,
                            const std::vector<int>& fragValues) {
    // YUV textures require additional dimensions not yet modeled.
    if (fragValues[FD::HAS_YUV] != 0) {
      return false;
    }
    // ALPHA_ONLY and HAS_RGBAAA are mutually exclusive in practice.
    if (fragValues[FD::ALPHA_ONLY] != 0 && fragValues[FD::HAS_RGBAAA] != 0) {
      return false;
    }
    // Vertex domain does not include HAS_XP or HAS_COVERAGE — vert variants must match frag base
    // dimensions. Each vert variant pairs with all HAS_XP and HAS_COVERAGE frag variants.
    if (vertValues[VD::HAS_YUV] != fragValues[FD::HAS_YUV] ||
        vertValues[VD::ALPHA_ONLY] != fragValues[FD::ALPHA_ONLY] ||
        vertValues[VD::HAS_RGBAAA] != fragValues[FD::HAS_RGBAAA] ||
        vertValues[VD::HAS_SUBSET] != fragValues[FD::HAS_SUBSET]) {
      return false;
    }
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TextureFillShader)
