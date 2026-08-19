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

class TextureColorMatrixShader : public PrecompiledShader {
 public:
  // ALPHA_ONLY / HAS_RGBAAA are folded into runtime uniforms (AlphaOnly / HasRgbaaa) written by
  // GLSLTextureEffect::onSetData, not compile-time permutations. Subset clamping is also runtime:
  // the shader always declares Subset and clamps, and the writer uploads the full texture bounds
  // when there is no real subset, so the clamp degenerates to a no-op.
  struct Dims {
    enum : uint32_t { HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using D = Dims;
  static_assert(D::COUNT == 1, "Update the matcher when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"TextureColorMatrixShader",
            "level1/texture_color_matrix.vert",
            "level1/texture_color_matrix.frag",
            PermutationDomain({}),
            D::domain(),
            PermutationDomain({}),
            "",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TextureColorMatrixShader)
