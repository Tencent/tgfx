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
 * ComplexEllipseFillShader: RRectDrawOp's complex AA ellipse form (per-corner radii), no color
 * fragment processors. A sole coverage FP is served through the shared coverage contract: an AA
 * rect/rrect clip rides the runtime clip uniforms and a device-space mask is the runtime
 * HasDeviceMask uniform over the always-declared (padded) mask sampler. LocalMask needs a
 * per-vertex UV this geometry does not provide and stays rejected.
 */
class ComplexEllipseFillShader : public FillKernel<ComplexEllipseFillShader> {
 public:
  static constexpr const char* Name = "ComplexEllipseFillShader";
  static constexpr const char* VertFile = "kernels/complex_ellipse_fill.vert";
  static constexpr const char* FragFile = "kernels/complex_ellipse_fill.frag";
  static constexpr bool ServesAlphaOnly = false;

  static constexpr Dim vertDims[] = {{"STROKE", 2}};
  static constexpr Dim fragDims[] = {{"STROKE", 2}, {XP_DEFINE_NAME, XP_VALUE_COUNT}};

  static bool Accept(const DrawShape& shape) {
    if (shape.geometry.kind != GeometryKind::ComplexEllipse) {
      return false;
    }
    if (shape.colorFPCount != 0) {
      return false;
    }
    if (!shape.coverage.has_value() || shape.coverage == Coverage::LocalMask) {
      return false;
    }
    return shape.xpType() >= XP_NONE;
  }

  static Variants Select(const DrawShape& shape) {
    Variants variants;
    variants.vert("STROKE", shape.geometry.isStroke ? 1 : 0)
        .frag("STROKE", shape.geometry.isStroke ? 1 : 0)
        .frag(XP_DEFINE_NAME, shape.xpType());
    return variants;
  }
};

}  // namespace tgfx
