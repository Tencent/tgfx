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
  struct Dims {
    // HAS_COVERAGE carries the geometry processor's AA coverage varying: the runtime composites
    // it into the sampled device-space coverage (texture.r * vCoverage), so it must be a
    // mirrored vert/frag dimension. HAS_XP rides the same domain.
    enum : uint32_t { HAS_COVERAGE, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = Dims;
  static_assert(FD::COUNT == 2, "Update info() when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"DeviceSpaceTextureShader",
            "level1/device_space_texture.vert",
            "level1/device_space_texture.frag",
            FD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::DeviceSpaceTextureShader)
