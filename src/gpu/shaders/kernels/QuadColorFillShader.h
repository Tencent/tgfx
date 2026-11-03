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
 * QuadColorFillShader: QuadPerEdgeAAGeometryProcessor with the per-vertex color only. Accepts
 * zero fragment processors, one direct analytic clip (AA rect or rrect, evaluated by the runtime
 * clip contract uniforms), or one device-space mask (the HAS_MASK_TEXTURE sampler).
 */
class QuadColorFillShader : public FillKernel<QuadColorFillShader> {
 public:
  static constexpr const char* Name = "QuadColorFillShader";
  static constexpr const char* VertFile = "kernels/quad_color_fill.vert";
  static constexpr const char* FragFile = "kernels/quad_color_fill.frag";
  static constexpr bool ServesAlphaOnly = false;

  /// Fragment dimensions, in the bundle's dimension order:
  ///   HAS_XP: 0=passthrough, 1=dst texture, 2=framebuffer fetch
  ///   HAS_MASK_TEXTURE: device-space mask texture binding is present
  static constexpr Dim fragDims[] = {{XP_DEFINE_NAME, XP_VALUE_COUNT}, {"HAS_MASK_TEXTURE", 2}};

  static bool Accept(const DrawShape& shape) {
    if (shape.geometry.kind != GeometryKind::QuadPerEdgeAA) {
      return false;
    }
    if (shape.fragmentCount != 0) {
      if (shape.colorFPCount != 0) {
        return false;
      }
      if (shape.coverage != Coverage::AARect && shape.coverage != Coverage::RRect &&
          shape.coverage != Coverage::DeviceMask) {
        return false;
      }
    }
    return shape.xpType() >= XP_NONE;
  }

  static Variants Select(const DrawShape& shape) {
    Variants variants;
    variants.frag(XP_DEFINE_NAME, shape.xpType())
        .frag("HAS_MASK_TEXTURE", shape.coverage == Coverage::DeviceMask ? 1 : 0);
    return variants;
  }
};

}  // namespace tgfx
