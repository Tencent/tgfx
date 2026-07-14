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

/// Precompiled shader declaration for TiledTextureEffect. Handles texture sampling with various
/// tiling modes (repeat, mirror, clamp-to-border, etc.) applied per-axis in shader code.
///
/// Fragment dimensions:
///   SHADER_MODE_X (int, 9 values): X-axis tiling mode (None..ClampToBorderLinear)
///   SHADER_MODE_Y (int, 9 values): Y-axis tiling mode
///   ALPHA_ONLY (bool): texture is alpha-only format
///   HAS_STRICT (bool): SrcRectConstraint::Strict is active
///
/// Vertex dimensions:
///   HAS_PERSPECTIVE (bool): coordinate transform has perspective
class TiledTextureFillShader : public PrecompiledShader {
 public:
  // Fragment dimensions
  struct FragDims {
    enum : uint32_t { SHADER_MODE_X, SHADER_MODE_Y, ALPHA_ONLY, HAS_STRICT, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("SHADER_MODE_X", 9),
          PermutationInt("SHADER_MODE_Y", 9),
          PermutationBool("ALPHA_ONLY"),
          PermutationBool("HAS_STRICT"),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 5, "Update ShouldCompile when fragment dimensions change.");

  // Vertex dimensions
  TGFX_DEFINE_DIMS(HAS_PERSPECTIVE);
  using VD = Dims;
  static_assert(VD::COUNT == 1, "Update info() when vertex dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"TiledTextureFillShader",
            "level1/tiled_texture_fill.vert",
            "level1/tiled_texture_fill.frag",
            VD::domain(),
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
    int modeX = fragValues[FD::SHADER_MODE_X];
    int modeY = fragValues[FD::SHADER_MODE_Y];
    // When both axes are None, tiling is fully handled by hardware sampler — no shader needed.
    // This combination never occurs in practice because TiledTextureEffect is only created when
    // at least one axis requires shader-based tiling.
    if (modeX == 0 && modeY == 0) {
      return false;
    }
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TiledTextureFillShader)
