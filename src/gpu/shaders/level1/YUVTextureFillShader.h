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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

/// Precompiled shader for QuadPerEdgeAAGeometryProcessor + TextureEffect with a YUV texture.
/// The multi-plane sampling and color conversion are uniform-driven except the plane layout
/// itself: I420 binds three plane samplers and NV12 two, so the format is the only compile-time
/// dimension besides the usual HAS_XP.
///
/// Vertex dimensions:
///   HAS_UV_COORD (bool): explicit UV coordinate attribute present.
/// Fragment dimensions:
///   YUV_FORMAT (int, 2 values): 0 = I420 (three planes), 1 = NV12 (two planes).
///   HAS_XP (int, 3 values): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF.
class YUVTextureFillShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { HAS_UV_COORD, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_UV_COORD"),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { YUV_FORMAT, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("YUV_FORMAT", 2),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;

  PrecompiledShaderInfo info() const override {
    return {"YUVTextureFillShader",
            "level1/yuv_texture_fill.vert",
            "level1/yuv_texture_fill.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            nullptr};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::YUVTextureFillShader)
