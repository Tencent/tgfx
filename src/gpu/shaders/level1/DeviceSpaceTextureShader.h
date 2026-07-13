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
/// Fragment dimensions:
///   ALPHA_ONLY (bool): whether the texture is alpha-only format
class DeviceSpaceTextureShader : public PrecompiledShader {
 public:
  TGFX_DEFINE_DIMS(ALPHA_ONLY, HAS_XP);
  using FD = Dims;
  static_assert(FD::COUNT == 2, "Update info() when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"DeviceSpaceTextureShader",
            "level1/device_space_texture.vert",
            "level1/device_space_texture.frag",
            PermutationDomain({}),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            nullptr};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::DeviceSpaceTextureShader)
