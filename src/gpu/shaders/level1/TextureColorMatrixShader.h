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
  TGFX_DEFINE_DIMS(ALPHA_ONLY, HAS_RGBAAA, HAS_SUBSET);
  using D = Dims;
  static_assert(D::COUNT == 3, "Update ShouldCompile below when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"TextureColorMatrixShader",
            "level1/texture_color_matrix.vert",
            "level1/texture_color_matrix.frag",
            PermutationDomain({}),
            D::domain(),
            PermutationDomain({}),
            "",
            "",
            ShouldCompile};
  }

 private:
  static bool ShouldCompile(uint32_t /*vertIndex*/, uint32_t /*fragIndex*/,
                            const std::vector<int>& /*vertValues*/,
                            const std::vector<int>& fragValues) {
    bool alphaOnly = fragValues[D::ALPHA_ONLY] != 0;
    bool hasRgbaaa = fragValues[D::HAS_RGBAAA] != 0;
    // ALPHA_ONLY and HAS_RGBAAA are mutually exclusive in practice.
    return !(alphaOnly && hasRgbaaa);
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TextureColorMatrixShader)
