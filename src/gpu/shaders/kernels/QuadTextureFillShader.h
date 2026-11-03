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
 * QuadTextureFillShader: QuadPerEdgeAAGeometryProcessor with exactly one TextureEffect color.
 * Coverage is optional: a device-space mask is a runtime uniform, a local-space texture-alpha
 * mask maps to HAS_LOCAL_MASK. The color leaf must be the IR's bare plain-sampled texture (no
 * operators above it), so the acceptance is defined in the family's .cpp.
 */
class QuadTextureFillShader : public FillKernel<QuadTextureFillShader> {
 public:
  static constexpr const char* Name = "QuadTextureFillShader";
  static constexpr const char* VertFile = "kernels/quad_texture_fill.vert";
  static constexpr const char* FragFile = "kernels/quad_texture_fill.frag";
  static constexpr bool ServesAlphaOnly = true;

  /// Dimensions, in the bundle's dimension order:
  ///   vert: HAS_UV_COORD (uvCoord attribute vs matrix), HAS_SUBSET, HAS_LOCAL_MASK
  ///   frag: HAS_SUBSET, HAS_XP, HAS_LOCAL_MASK
  static constexpr Dim vertDims[] = {{"HAS_UV_COORD", 2}, {"HAS_SUBSET", 2}, {"HAS_LOCAL_MASK", 2}};
  static constexpr Dim fragDims[] = {{"HAS_SUBSET", 2},
                                     {XP_DEFINE_NAME, XP_VALUE_COUNT},
                                     {"HAS_LOCAL_MASK", 2}};

  static bool Accept(const DrawShape& shape);
  static Variants Select(const DrawShape& shape);
};

}  // namespace tgfx
