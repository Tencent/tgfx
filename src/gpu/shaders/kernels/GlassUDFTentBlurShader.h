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
 * Precompiled shader for GlassUDFTentBlurFragmentProcessor: blurs alpha coverage with a tent
 * kernel in one direction and writes one field into an RGBA8 target. The tent weights are
 * computed analytically inside the shader, so no kernel table is uploaded. The blur barrier's
 * facts come off the carried IR's opaque-kernel node, so the acceptance is defined in the
 * family's .cpp.
 */
class GlassUDFTentBlurShader : public FillKernel<GlassUDFTentBlurShader> {
 public:
  static constexpr const char* Name = "GlassUDFTentBlurShader";
  static constexpr const char* VertFile = "kernels/blur_texture.vert";
  static constexpr const char* FragFile = "kernels/glass_udf_tent_blur.frag";
  static constexpr bool ServesAlphaOnly = false;

  /// Fragment dimensions: HAS_XP (0=passthrough, 1=dst texture, 2=framebuffer fetch).
  static constexpr Dim fragDims[] = {{XP_DEFINE_NAME, XP_VALUE_COUNT}};

  static bool Accept(const DrawShape& shape);
  static Variants Select(const DrawShape& shape);
};

}  // namespace tgfx
