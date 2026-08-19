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
    enum : uint32_t { HAS_COVERAGE, HAS_COMMON_COLOR, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_COMMON_COLOR"),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(D::COUNT == 2 && FD::COUNT == 3,
                "Update the Compose mapping when dimensions change.");

 public:
  PrecompiledShaderInfo info() const override {
    return {"AtlasTextFillShader",
            "level1/atlas_text_fill.vert",
            "level1/atlas_text_fill.frag",
            D::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::AtlasTextFillShader)
