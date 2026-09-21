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

/// Precompiled shader declaration for DeviceSpaceTextureEffect. Samples a texture using
/// device-space (screen-space) coordinates derived from gl_FragCoord, rather than vertex UVs.
///
/// ALPHA_ONLY is a runtime uniform (AlphaOnly), written by GLSLDeviceSpaceTextureEffect::onSetData,
/// not a compile-time permutation.
class DeviceSpaceTextureShader : public PrecompiledShader {
 public:
  struct VertDims {
    // HAS_COVERAGE adds the AA coverage attribute/varying pair, so it stays a vertex dimension.
    // HAS_XP is fragment-only (the vertex stage never branches on it), so the vertex domain
    // carries coverage alone.
    enum : uint32_t { HAS_COVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { HAS_COVERAGE, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(VD::COUNT == 1 && FD::COUNT == 2,
                "Update info() and the Compose mapping when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"DeviceSpaceTextureShader",
            "level1/device_space_texture.vert",
            "level1/device_space_texture.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::DeviceSpaceTextureShader)
