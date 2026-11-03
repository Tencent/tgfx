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
 * MaskFillShader: ShapeDrawOp's DefaultGeometryProcessor with a solid color masked by a single
 * alpha-only coverage texture (a rasterized shape mask). The mask's sampler facts come off the
 * IR's sole coverage term, so the acceptance is defined in the family's .cpp.
 */
class MaskFillShader : public FillKernel<MaskFillShader> {
 public:
  static constexpr const char* Name = "MaskFillShader";
  static constexpr const char* VertFile = "kernels/plain_texture.vert";
  static constexpr const char* FragFile = "kernels/mask_fill.frag";
  static constexpr bool ServesAlphaOnly = true;

  /// Fragment dimensions: HAS_XP (0=passthrough, 1=dst texture, 2=framebuffer fetch).
  static constexpr Dim fragDims[] = {{XP_DEFINE_NAME, XP_VALUE_COUNT}};

  static bool Accept(const DrawShape& shape);
  static Variants Select(const DrawShape& shape);
};

}  // namespace tgfx
