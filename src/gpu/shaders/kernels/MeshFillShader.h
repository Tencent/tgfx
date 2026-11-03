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
 * MeshFillShader: MeshDrawOp's triangle mesh, no fragment processors. The vertex stage's
 * attribute list carries texture coordinates or not, selected by the HAS_TEX_COORDS dimension.
 */
class MeshFillShader : public FillKernel<MeshFillShader> {
 public:
  static constexpr const char* Name = "MeshFillShader";
  static constexpr const char* VertFile = "kernels/mesh_fill.vert";
  static constexpr const char* FragFile = "kernels/mesh_fill.frag";
  static constexpr bool ServesAlphaOnly = false;

  static constexpr Dim vertDims[] = {{"HAS_TEX_COORDS", 2}};
  /// Fragment dimensions: HAS_XP (0=passthrough, 1=dst texture, 2=framebuffer fetch).
  static constexpr Dim fragDims[] = {{XP_DEFINE_NAME, XP_VALUE_COUNT}};

  static bool Accept(const DrawShape& shape) {
    return shape.geometry.kind == GeometryKind::Mesh && shape.fragmentCount == 0 &&
           shape.xpType() >= XP_NONE;
  }

  static Variants Select(const DrawShape& shape) {
    Variants variants;
    variants.vert("HAS_TEX_COORDS", shape.geometry.hasTexCoords ? 1 : 0)
        .frag(XP_DEFINE_NAME, shape.xpType());
    return variants;
  }
};

}  // namespace tgfx
