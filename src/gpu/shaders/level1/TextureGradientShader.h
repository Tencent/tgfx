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

class TextureGradientShader : public PrecompiledShader {
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
    enum : uint32_t { LAYOUT_TYPE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationEnum("LAYOUT_TYPE", {"LINEAR", "RADIAL", "CONIC", "DIAMOND"}),
      });
    }
  };
  using FD = FragDims;

  PrecompiledShaderInfo info() const override {
    return {"TextureGradientShader",
            "level1/gradient_fill.vert",
            "level1/texture_gradient.frag",
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
                            const std::vector<int>& /*fragValues*/) {
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::TextureGradientShader)
