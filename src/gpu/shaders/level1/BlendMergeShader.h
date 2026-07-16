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

/// Precompiled shader declaration for XfermodeFragmentProcessor. Blends two color inputs using one
/// of 30 blend modes. The child type determines how inputs are sourced.
///
/// Vertex dimensions:
///   GP_TYPE (int, 2 values): 0=DefaultGeometryProcessor, 1=QuadPerEdgeAAGeometryProcessor
///   HAS_COVERAGE (bool): whether QuadGP provides per-vertex AA coverage
///   HAS_UV_COORD (bool): whether QuadGP has uvCoord attribute (coord source = uvCoord vs position)
///   HAS_COLOR (bool): whether QuadGP provides per-vertex color attribute
///
/// Fragment dimensions:
///   CHILD_TYPE (int, 3 values): 0=DstChild, 1=SrcChild, 2=TwoChild
///   HAS_XP (int, 3 values): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF
///   CHILD0_MODE (int, 3 values): 0=TextureEffect, 1=ConstColor, 2=TiledTextureEffect
///   HAS_CHILD_SUBSET (int, 4 values): bitmask of which texture children need subset clamping.
///     bit0 -> child[0] (valid only when CHILD0_MODE=0), bit1 -> child[1] (valid only when
///     CHILD_TYPE=2). See blend_merge.frag for details.
///   HAS_COVERAGE (bool): matches vert dimension, controls vCoverage varying input
///   HAS_COLOR (bool): matches vert dimension, controls vColor varying input
///
/// DstChild: input color is src, child texture provides dst
/// SrcChild: child texture provides src, input color is dst
/// TwoChild: two child textures provide src and dst respectively
class BlendMergeShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { GP_TYPE, HAS_COVERAGE, HAS_UV_COORD, HAS_COLOR, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_TYPE", 2),
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_UV_COORD"),
          PermutationBool("HAS_COLOR"),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t {
      CHILD_TYPE,
      HAS_XP,
      CHILD0_MODE,
      HAS_CHILD_SUBSET,
      HAS_COVERAGE,
      HAS_COLOR,
      HAS_MASK_TEXTURE,
      COUNT
    };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("CHILD_TYPE", 3),
          PermutationInt("HAS_XP", 3),
          PermutationInt("CHILD0_MODE", 3),
          PermutationInt("HAS_CHILD_SUBSET", 4),
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_COLOR"),
          PermutationBool("HAS_MASK_TEXTURE"),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 7, "Update ShouldCompile when fragment dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"BlendMergeShader",
            "level1/blend_merge.vert",
            "level1/blend_merge.frag",
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
    int childType = fragValues[FD::CHILD_TYPE];
    int child0Mode = fragValues[FD::CHILD0_MODE];
    int subsetMask = fragValues[FD::HAS_CHILD_SUBSET];
    // bit0 (child[0] subset) is only meaningful for a plain TextureEffect child (CHILD0_MODE=0);
    // ConstColor and TiledTextureEffect children have no plain subset uniform.
    if ((subsetMask & 1) != 0 && child0Mode != 0) {
      return false;
    }
    // bit1 (child[1] subset) is only meaningful in TwoChild mode (CHILD_TYPE=2), where child[1] is
    // sampled via TextureSampler_1.
    if ((subsetMask & 2) != 0 && childType != 2) {
      return false;
    }
    // HAS_COVERAGE and HAS_COLOR must match between vert and frag (varying declarations must agree).
    if (vertValues[VD::HAS_COVERAGE] != fragValues[FD::HAS_COVERAGE]) {
      return false;
    }
    if (vertValues[VD::HAS_COLOR] != fragValues[FD::HAS_COLOR]) {
      return false;
    }
    // DefaultGP (GP_TYPE=0) never has per-vertex coverage, uvCoord, or color — those are
    // QuadGP-only features.
    int gpType = vertValues[VD::GP_TYPE];
    if (gpType == 0 && (vertValues[VD::HAS_COVERAGE] != 0 || vertValues[VD::HAS_UV_COORD] != 0 ||
                        vertValues[VD::HAS_COLOR] != 0)) {
      return false;
    }
    // HAS_MASK_TEXTURE is only used with DefaultGP (GP_TYPE=0) in practice because mask clips
    // go through DefaultGeometryProcessor paths.
    if (fragValues[FD::HAS_MASK_TEXTURE] != 0 && gpType != 0) {
      return false;
    }
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::BlendMergeShader)
