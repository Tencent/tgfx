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
/// tiling modes (repeat, mirror, clamp-to-border, etc.) applied per-axis via runtime uniform
/// branches. ShaderModeX/ShaderModeY are passed as uniforms rather than compile-time permutations
/// to dramatically reduce variant count (from 640 to 12).
///
/// Fragment dimensions:
///   ALPHA_ONLY (bool): texture is alpha-only format
///   HAS_STRICT (bool): SrcRectConstraint::Strict is active
///   HAS_XP (int, 3 values): XferProcessor type
///
/// Vertex dimensions:
///   HAS_PERSPECTIVE (bool): coordinate transform has perspective
class TiledTextureFillShader : public PrecompiledShader {
 public:
  // Fragment dimensions
  struct FragDims {
    enum : uint32_t { ALPHA_ONLY, HAS_STRICT, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("ALPHA_ONLY"),
          PermutationBool("HAS_STRICT"),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 3, "Update info() when fragment dimensions change.");

  // Vertex dimensions
  struct VertDims {
    enum : uint32_t { GP_TYPE, HAS_PERSPECTIVE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_TYPE", 2),
          PermutationBool("HAS_PERSPECTIVE"),
      });
    }
  };
  using VD = VertDims;
  static_assert(VD::COUNT == 2, "Update info() when vertex dimensions change.");

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
                            const std::vector<int>& vertValues,
                            const std::vector<int>& /*fragValues*/) {
    // The QuadPerEdgeAA path pre-transforms vertices to device space and carries no perspective
    // coordinate; only the DefaultGeometryProcessor path emits a perspective coord.
    if (vertValues[VD::GP_TYPE] == 1 && vertValues[VD::HAS_PERSPECTIVE] != 0) {
      return false;
    }
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TiledTextureFillShader)
