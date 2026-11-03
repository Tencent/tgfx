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
 * Cold "uber" shader for the (1 sampler, no CoverageFP, RGBA) structural class: samples one
 * intermediate texture then applies exactly one pointwise operator selected at runtime by the
 * OpType uniform (0=ColorMatrix, 1=Luma, 2=AlphaThreshold, 3=ColorSpaceXform). The operator
 * identity and the texture-leaf shape come off the carried IR, so the acceptance is defined in
 * the family's .cpp. The coverage attribute is unconditional, so there is no HAS_COVERAGE
 * dimension.
 */
class TexturedEffectShader : public FillKernel<TexturedEffectShader> {
 public:
  static constexpr const char* Name = "TexturedEffectShader";
  static constexpr const char* VertFile = "kernels/textured_effect.vert";
  static constexpr const char* FragFile = "kernels/textured_effect.frag";
  static constexpr bool ServesAlphaOnly = false;

  /// Fragment dimensions: HAS_XP (0=passthrough, 1=dst texture, 2=framebuffer fetch).
  static constexpr Dim fragDims[] = {{XP_DEFINE_NAME, XP_VALUE_COUNT}};

  static bool Accept(const DrawShape& shape);
  static Variants Select(const DrawShape& shape);
};

}  // namespace tgfx
