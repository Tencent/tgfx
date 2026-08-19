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

/// Precompiled shader for a solid-color fill masked by an alpha-only coverage texture, drawn through
/// DefaultGeometryProcessor with a single TextureEffect coverage fragment processor. Covers the
/// common ShapeDrawOp mask path (a rasterized shape used as coverage). The fill color comes from the
/// geometry processor's Color uniform; the mask is sampled per-vertex via CoordTransformMatrix_0.
///
/// The mask coverage feeds the XferProcessor's coverage lerp, so Porter-Duff blends (DST_TEX and
/// framebuffer-fetch) are supported via the HAS_XP fragment dimension, exactly like the other fill
/// shaders. The empty transfer path keeps the original straight multiply (Color * maskCoverage).
class MaskFillShader : public PrecompiledShader {
 public:
  struct FragDims {
    enum : uint32_t { HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using D = FragDims;

  PrecompiledShaderInfo info() const override {
    return {"MaskFillShader",
            "level1/mask_fill.vert",
            "level1/mask_fill.frag",
            PermutationDomain({}),
            D::domain(),
            PermutationDomain({}),
            "",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::MaskFillShader)
