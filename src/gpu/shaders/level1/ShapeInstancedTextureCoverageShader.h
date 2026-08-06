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
  PrecompiledShaderInfo info() const override {
    return {"ShapeInstancedTextureCoverageShader",
            "level1/shape_instanced_texture_coverage.vert",
            "level1/shape_instanced_texture_coverage.frag",
            PermutationDomain({}),
            PermutationDomain({}),
            PermutationDomain({}),
            "ShapeInstancedGeometryProcessor",
            "",
            nullptr};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::ShapeInstancedTextureCoverageShader)
