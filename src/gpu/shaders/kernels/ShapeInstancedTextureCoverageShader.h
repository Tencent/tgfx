/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
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

#include "gpu/shaders/FillKernel.h"

namespace tgfx {

/**
 * ShapeInstancedTextureCoverageShader: ShapeInstancedDrawOp's instanced shape batch in its
 * non-AA form, whose coverage comes from a texture (optionally topped by a single-interval
 * gradient and/or an analytic clip). The coverage texture's and the gradient's facts come off
 * the carried IR's terms and color graph, so the acceptance is defined in the family's .cpp.
 */
class ShapeInstancedTextureCoverageShader : public FillKernel<ShapeInstancedTextureCoverageShader> {
 public:
  static constexpr const char* Name = "ShapeInstancedTextureCoverageShader";
  static constexpr const char* VertFile = "kernels/shape_instanced_texture_coverage.vert";
  static constexpr const char* FragFile = "kernels/shape_instanced_texture_coverage.frag";
  static constexpr bool ServesAlphaOnly = false;

  static constexpr Dim vertDims[] = {{"GRADIENT", 2}};
  static constexpr Dim fragDims[] = {{"GRADIENT", 2}};

  static bool Accept(const DrawShape& shape);
  static Variants Select(const DrawShape& shape);
};

}  // namespace tgfx
