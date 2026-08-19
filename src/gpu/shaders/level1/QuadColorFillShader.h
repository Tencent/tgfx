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
  // No vertex dimensions: coverage and color are both unconditional attributes.
  struct VertDims {
    static PermutationDomain domain() {
      return PermutationDomain({});
    }
  };
  using VD = VertDims;

  // Fragment dimensions (adds HAS_XP and HAS_MASK_TEXTURE). HAS_MASK_TEXTURE is orthogonal to the
  // per-vertex geometry AA coverage (HAS_COVERAGE): it samples a device-space mask texture and
  // multiplies it into the color, composing with the geometry AA. Placed last to keep existing
  // dimension indices stable.
  struct FragDims {
    enum : uint32_t { HAS_XP, HAS_MASK_TEXTURE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_MASK_TEXTURE"),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 2, "Update the Compose mapping when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"QuadColorFillShader",
            "level1/quad_color_fill.vert",
            "level1/quad_color_fill.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "QuadPerEdgeAAGeometryProcessor",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::QuadColorFillShader)
