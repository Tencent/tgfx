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
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

class ShapeInstancedTextureCoverageShader : public PrecompiledShader {
 public:
  // GRADIENT (bool): the color source is a single-interval gradient instead of the per-instance
  // vertex color; its layout is selected at runtime through the LayoutType uniform.
  // HAS_COLORS is not a dimension: the instance color attribute is unconditional (colorless
  // batches broadcast white, matching the runtime codegen's implicit color), so both the bare
  // coverage form and the gradient form read the same vColor varying.
  TGFX_DEFINE_DIMS(GRADIENT);
  using D = Dims;

  struct FragDims {
    enum : uint32_t { GRADIENT, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("GRADIENT"),
      });
    }
  };
  using FD = FragDims;

  PrecompiledShaderInfo info() const override {
    return {"ShapeInstancedTextureCoverageShader",
            "level1/shape_instanced_texture_coverage.vert",
            "level1/shape_instanced_texture_coverage.frag",
            D::domain(),
            FD::domain(),
            PermutationDomain({}),
            "ShapeInstancedGeometryProcessor",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::ShapeInstancedTextureCoverageShader,
                     tgfx::EnumerateShapeInstancedTextureCoverageReachable)
