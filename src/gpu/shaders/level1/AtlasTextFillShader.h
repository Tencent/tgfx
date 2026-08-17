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
  // ALPHA_ONLY is a runtime uniform (AlphaOnly), written by GLSLAtlasTextGeometryProcessor::setData,
  // not a permutation dimension — the vertex stage never uses it.
  TGFX_DEFINE_DIMS(HAS_COVERAGE, HAS_COMMON_COLOR);
  using D = Dims;

  struct FragDims {
    // A device-space mask is a runtime uniform (HasDeviceMask), not a permutation dimension.
    // TEXTURE_KIND is the atlas sampler type (0=TwoD, 1=Rect). The atlas is a rectangle texture on
    // desktop GL; it changes the sampler declaration, so it is a legitimate dimension. Appended
    // last so TwoD variant indices are unchanged. Rect variants compile only into the opengl bundle.
    enum : uint32_t { HAS_COVERAGE, HAS_COMMON_COLOR, HAS_XP, TEXTURE_KIND, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_COMMON_COLOR"),
          PermutationInt("HAS_XP", 3),
          PermutationEnum("TEXTURE_KIND", {"TwoD", "Rect"}),
      });
    }
  };
  using FD = FragDims;
  static_assert(D::COUNT == 2 && FD::COUNT == 4,
                "Update ShouldCompile below when dimensions change.");

 public:
  PrecompiledShaderInfo info() const override {
    return {"AtlasTextFillShader",
            "level1/atlas_text_fill.vert",
            "level1/atlas_text_fill.frag",
            D::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            ShouldCompile};
  }

 private:
  static bool ShouldCompile(uint32_t, uint32_t, const std::vector<int>&,
                            const std::vector<int>& fragValues) {
    // Framebuffer fetch (HAS_XP=2) is a Vulkan/Metal-only path; Rect variants are opengl-only and
    // never carry FBF.
    return !(fragValues[FD::TEXTURE_KIND] == 1 && fragValues[FD::HAS_XP] == 2);
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::AtlasTextFillShader)
