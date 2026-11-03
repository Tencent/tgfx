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
 * EllipseFillShader fragment entry: EllipseGeometryProcessor + EmptyXferProcessor/PorterDuffXP,
 * no color fragment processor. The ellipse edge coverage is true coverage; a device mask and the
 * runtime clip multiply into it, a local-mask texture is not served (the vertex stage provides no
 * per-vertex UV). The color attribute is unconditional and the exact painting color rides the
 * HasCommonColor runtime uniform, so HAS_COMMON_COLOR is not a dimension.
 */
class EllipseFillShader : public FillKernel<EllipseFillShader> {
 public:
  static constexpr const char* Name = "EllipseFillShader";
  static constexpr const char* VertFile = "kernels/ellipse_fill.vert";
  static constexpr const char* FragFile = "kernels/ellipse_fill.frag";
  static constexpr bool ServesAlphaOnly = false;

  /// Fragment dimensions, in the bundle's dimension order:
  ///   HAS_XP: 0=passthrough, 1=PorterDuff dst texture, 2=PorterDuff framebuffer fetch
  ///   HAS_DEVICE_MASK: device-space mask texture binding is present
  static constexpr Dim fragDims[] = {{XP_DEFINE_NAME, XP_VALUE_COUNT}, {"HAS_DEVICE_MASK", 2}};

  static bool Accept(const DrawShape& shape) {
    return shape.geometry.kind == GeometryKind::Ellipse && shape.colorFPCount == 0 &&
           shape.coverage.has_value() && shape.coverage != Coverage::LocalMask &&
           shape.xpType() >= XP_NONE;
  }

  static Variants Select(const DrawShape& shape) {
    Variants variants;
    variants.frag(XP_DEFINE_NAME, shape.xpType())
        .frag("HAS_DEVICE_MASK", shape.coverage == Coverage::DeviceMask ? 1 : 0);
    return variants;
  }
};

}  // namespace tgfx
