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
#include "gpu/shaders/ShaderPermutation.h"

namespace tgfx {

/// Precompiled shader for QuadPerEdgeAAGeometryProcessor with zero fragment processors.
/// Covers solid-color AA quad rendering with optional per-vertex coverage and per-vertex color.
class QuadColorFillShader : public PrecompiledShader {
 public:
  // Vertex dimensions (unchanged — XP does not affect vertex stage)
  TGFX_DEFINE_DIMS(HAS_COVERAGE, HAS_COLOR);
  using VD = Dims;

  // Fragment dimensions (adds HAS_XP and HAS_MASK_TEXTURE). HAS_MASK_TEXTURE is orthogonal to the
  // per-vertex geometry AA coverage (HAS_COVERAGE): it samples a device-space mask texture and
  // multiplies it into the color, composing with the geometry AA. Placed last to keep existing
  // dimension indices stable.
  struct FragDims {
    enum : uint32_t { HAS_COVERAGE, HAS_COLOR, HAS_XP, HAS_MASK_TEXTURE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_COLOR"),
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_MASK_TEXTURE"),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 4, "Update ShouldCompile below when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"QuadColorFillShader",
            "level1/quad_color_fill.vert",
            "level1/quad_color_fill.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "QuadPerEdgeAAGeometryProcessor",
            "",
            ShouldCompile};
  }

 private:
  static bool ShouldCompile(uint32_t, uint32_t, const std::vector<int>& vertValues,
                            const std::vector<int>& fragValues) {
    // Vert and frag must agree on HAS_COVERAGE and HAS_COLOR.
    if (vertValues[VD::HAS_COVERAGE] != fragValues[FD::HAS_COVERAGE]) {
      return false;
    }
    if (vertValues[VD::HAS_COLOR] != fragValues[FD::HAS_COLOR]) {
      return false;
    }
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::QuadColorFillShader)
