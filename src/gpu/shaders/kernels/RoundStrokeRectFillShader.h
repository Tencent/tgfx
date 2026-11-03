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
 * RoundStrokeRectFillShader: RectDrawOp's round-capped/joined stroke rect. The kernel reads the
 * clip from the runtime clip contract uniforms, so a sole analytic clip is admitted alongside
 * the FP-free form. Perspective UV transforms are not covered by precompiled variants. The
 * color attribute is unconditional and the AA ramp is the AAEnabled runtime uniform, so neither
 * is a dimension.
 */
class RoundStrokeRectFillShader : public FillKernel<RoundStrokeRectFillShader> {
 public:
  static constexpr const char* Name = "RoundStrokeRectFillShader";
  static constexpr const char* VertFile = "kernels/round_stroke_rect_fill.vert";
  static constexpr const char* FragFile = "kernels/round_stroke_rect_fill.frag";
  static constexpr bool ServesAlphaOnly = false;

  static constexpr Dim vertDims[] = {{"HAS_UV_MATRIX", 2}};
  /// Fragment dimensions: HAS_XP (0=passthrough, 1=dst texture, 2=framebuffer fetch).
  static constexpr Dim fragDims[] = {{XP_DEFINE_NAME, XP_VALUE_COUNT}};

  static bool Accept(const DrawShape& shape) {
    if (shape.geometry.kind != GeometryKind::RoundStrokeRect) {
      return false;
    }
    bool directClip = shape.colorFPCount == 0 &&
                      (shape.coverage == Coverage::AARect || shape.coverage == Coverage::RRect);
    if (shape.fragmentCount != 0 && !directClip) {
      return false;
    }
    if (shape.geometry.hasUVPerspective) {
      return false;
    }
    return shape.xpType() >= XP_NONE;
  }

  static Variants Select(const DrawShape& shape) {
    Variants variants;
    variants.vert("HAS_UV_MATRIX", shape.geometry.hasUVMatrix ? 1 : 0)
        .frag(XP_DEFINE_NAME, shape.xpType());
    return variants;
  }
};

}  // namespace tgfx
