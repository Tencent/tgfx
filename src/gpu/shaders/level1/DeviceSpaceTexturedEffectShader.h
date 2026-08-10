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

/// Precompiled shader for ComposeFragmentProcessor(DeviceSpaceTextureEffect, pointwise op). The
/// texture is sampled from gl_FragCoord and then transformed by the shared runtime-selected
/// pointwise operator. The matcher currently admits ColorMatrix and Luma operators over RGBA
/// intermediate textures only.
class DeviceSpaceTexturedEffectShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { HAS_COVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { HAS_XP, HAS_COVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_COVERAGE"),
      });
    }
  };
  using FD = FragDims;

  PrecompiledShaderInfo info() const override {
    return {"DeviceSpaceTexturedEffectShader",
            "level1/device_space_texture.vert",
            "level1/device_space_textured_effect.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            nullptr};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::DeviceSpaceTexturedEffectShader)
