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
/// of 30 blend modes.
///
/// Vertex dimensions:
///   GP_TYPE (int, 2 values): 0=DefaultGeometryProcessor, 1=QuadPerEdgeAAGeometryProcessor
///   HAS_COVERAGE (bool): whether QuadGP provides per-vertex AA coverage
///   HAS_UV_COORD (bool): whether QuadGP has uvCoord attribute (coord source = uvCoord vs position)
///   HAS_COLOR (bool): whether QuadGP provides per-vertex color attribute
///
/// Fragment dimensions:
///   HAS_TWO_CHILDREN (bool): whether a second child operand exists
///   HAS_XP (int, 3 values): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF
///   CHILD0_MODE (int, 2 values): 0=TextureEffect, 1=ConstColor
///   HAS_COVERAGE (bool): matches vert dimension, controls vCoverage varying input
///   HAS_COLOR (bool): matches vert dimension, controls vColor varying input
///
/// The blend mode and the operand roles are runtime uniforms (BlendModeValue, ChildType). Only the
/// axes that change the pipeline interface stay compile-time: HAS_TWO_CHILDREN adds a sampler and the
/// Child1Subset uniform, and CHILD0_MODE decides whether child[0] occupies a sampler or the
/// ConstColor uniform. The former three-valued CHILD_TYPE dimension collapsed into
/// HAS_TWO_CHILDREN because distinguishing DstChild from SrcChild is pure operand assignment.
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
      HAS_TWO_CHILDREN,
      HAS_XP,
      CHILD0_MODE,
      HAS_COVERAGE,
      HAS_COLOR,
      HAS_MASK_TEXTURE,
      COUNT
    };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_TWO_CHILDREN"),
          PermutationInt("HAS_XP", 3),
          PermutationInt("CHILD0_MODE", 2),
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_COLOR"),
          PermutationBool("HAS_MASK_TEXTURE"),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 6, "Update ShouldCompile when fragment dimensions change.");

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
    // HAS_COVERAGE / HAS_COLOR vertex and fragment agreement is enforced automatically by the
    // framework (MirroredDimsAgree).
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
