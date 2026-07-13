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
  TGFX_DEFINE_DIMS(HAS_COVERAGE, HAS_COLOR);
  using D = Dims;
  static_assert(D::COUNT == 2, "Update ShouldCompile below when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"QuadColorFillShader",
            "level1/quad_color_fill.vert",
            "level1/quad_color_fill.frag",
            D::domain(),
            D::domain(),
            PermutationDomain({}),
            "QuadPerEdgeAAGeometryProcessor",
            "",
            ShouldCompile};
  }

 private:
  static bool ShouldCompile(uint32_t, uint32_t, const std::vector<int>&,
                            const std::vector<int>&) {
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::QuadColorFillShader)
