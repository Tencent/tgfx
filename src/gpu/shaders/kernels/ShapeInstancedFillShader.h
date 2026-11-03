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
 * ShapeInstancedFillShader: ShapeInstancedDrawOp's instanced shape batch, no fragment
 * processors. The per-instance attributes carry the shape's coverage (SDF or texture), so the
 * vertex stage needs no dimension.
 */
class ShapeInstancedFillShader : public FillKernel<ShapeInstancedFillShader> {
 public:
  static constexpr const char* Name = "ShapeInstancedFillShader";
  static constexpr const char* VertFile = "kernels/shape_instanced_fill.vert";
  static constexpr const char* FragFile = "kernels/shape_instanced_fill.frag";
  static constexpr bool ServesAlphaOnly = false;

  /// Fragment dimensions: HAS_XP (0=passthrough, 1=dst texture, 2=framebuffer fetch).
  static constexpr Dim fragDims[] = {{XP_DEFINE_NAME, XP_VALUE_COUNT}};

  static bool Accept(const DrawShape& shape) {
    return shape.geometry.kind == GeometryKind::ShapeInstanced && shape.fragmentCount == 0 &&
           shape.xpType() >= XP_NONE;
  }

  static Variants Select(const DrawShape& shape) {
    Variants variants;
    variants.frag(XP_DEFINE_NAME, shape.xpType());
    return variants;
  }
};

}  // namespace tgfx
