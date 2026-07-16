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

/// Precompiled shader for the EffectDecomposer 2-pass pipeline when the second FP is
/// ColorMatrixFragmentProcessor. The intermediate texture from the first pass is sampled and
/// then transformed through a color matrix.
class TexturedColorMatrixShader : public PrecompiledShader {
 public:
  struct FragDims {
    enum : uint32_t { HAS_SUBSET, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_SUBSET"),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using D = FragDims;

  PrecompiledShaderInfo info() const override {
    return {"TexturedColorMatrixShader",
            "level1/textured_color_matrix.vert",
            "level1/textured_color_matrix.frag",
            PermutationDomain({}),
            D::domain(),
            PermutationDomain({}),
            "",
            "",
            nullptr};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TexturedColorMatrixShader)
