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
 * SolidColorFillShader: DefaultGeometryProcessor + EmptyXferProcessor/PorterDuffXP, no color
 * fragment processor. The fill color comes from the DefaultGP Color uniform, so a color fragment
 * processor would be silently dropped and must reject. The only accepted FP is one direct
 * analytic clip coverage FP (AA rect or rrect), evaluated from the runtime clip contract
 * uniforms.
 */
class SolidColorFillShader : public FillKernel<SolidColorFillShader> {
 public:
  static constexpr const char* Name = "SolidColorFillShader";
  static constexpr const char* VertFile = "kernels/solid_color_fill.vert";
  static constexpr const char* FragFile = "kernels/solid_color_fill.frag";
  static constexpr bool ServesAlphaOnly = true;

  /// Fragment dimensions: HAS_XP (0=passthrough, 1=dst texture, 2=framebuffer fetch).
  static constexpr Dim fragDims[] = {{XP_DEFINE_NAME, XP_VALUE_COUNT}};

  static bool Accept(const DrawShape& shape) {
    if (shape.geometry.kind != GeometryKind::Default) {
      return false;
    }
    bool noProcessorsOrDirectClip =
        shape.fragmentCount == 0 ||
        (shape.colorFPCount == 0 &&
         (shape.coverage == Coverage::AARect || shape.coverage == Coverage::RRect));
    if (!noProcessorsOrDirectClip) {
      return false;
    }
    return shape.xpType() >= XP_NONE;
  }

  static Variants Select(const DrawShape& shape) {
    Variants variants;
    variants.frag(XP_DEFINE_NAME, shape.xpType());
    return variants;
  }
};

}  // namespace tgfx
