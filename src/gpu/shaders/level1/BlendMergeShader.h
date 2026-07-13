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
///
/// Fragment dimensions:
///   BLEND_MODE (int, 30 values): BlendMode enum (0=Clear .. 29=PlusDarker)
///   CHILD_TYPE (int, 3 values): 0=DstChild, 1=SrcChild, 2=TwoChild
///
/// DstChild: input color is src, child texture provides dst
/// SrcChild: child texture provides src, input color is dst
/// TwoChild: two child textures provide src and dst respectively
class BlendMergeShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { GP_TYPE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_TYPE", 2),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { BLEND_MODE, CHILD_TYPE, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("BLEND_MODE", 30),
          PermutationInt("CHILD_TYPE", 3),
          PermutationBool("HAS_XP"),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 3, "Update ShouldCompile when fragment dimensions change.");

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
  static bool ShouldCompile(uint32_t /*vertIndex*/, uint32_t /*fragIndex*/,
                            const std::vector<int>& /*vertValues*/,
                            const std::vector<int>& fragValues) {
    int blendMode = fragValues[FD::BLEND_MODE];
    int childType = fragValues[FD::CHILD_TYPE];

    // Clear (0) with any child type degenerates to transparent output — handled at runtime.
    if (blendMode == 0) {
      return false;
    }
    // Src (1) + DstChild (0): output = inputColor directly — no shader needed.
    if (blendMode == 1 && childType == 0) {
      return false;
    }
    // Dst (2) + SrcChild (1): output = inputColor directly — no shader needed.
    if (blendMode == 2 && childType == 1) {
      return false;
    }
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::BlendMergeShader)
