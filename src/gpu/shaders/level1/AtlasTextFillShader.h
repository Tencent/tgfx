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

class AtlasTextFillShader : public PrecompiledShader {
 public:
  TGFX_DEFINE_DIMS(HAS_COVERAGE, HAS_COMMON_COLOR, ALPHA_ONLY);
  using D = Dims;
  static_assert(D::COUNT == 3, "Update ShouldCompile below when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"AtlasTextFillShader",
            "level1/atlas_text_fill.vert",
            "level1/atlas_text_fill.frag",
            D::domain(),
            D::domain(),
            PermutationDomain({}),
            "",
            "",
            ShouldCompile};
  }

 private:
  static bool ShouldCompile(uint32_t vertIndex, uint32_t fragIndex, const std::vector<int>&,
                            const std::vector<int>&) {
    // Both stages share the same domain, so only compile matching pairs.
    return vertIndex == fragIndex;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::AtlasTextFillShader)
