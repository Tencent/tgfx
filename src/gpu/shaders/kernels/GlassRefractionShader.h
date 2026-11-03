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
 * GlassRefractionShader: the refraction effect's opaque-kernel family. The geometry selects the
 * vertex layout (common-color ellipse vs uv-matrix quad); the refraction barrier's geometry
 * child and dispersion flag come off the carried IR's opaque-kernel node, so the acceptance and
 * the IR-derived part of the variant selection are defined in the family's .cpp. Dispersion is
 * a compile-time branch because the runtime emission compiles one path or the other and never
 * both — the precompiled variant must mirror that structure exactly to keep pixels identical.
 */
class GlassRefractionShader : public FillKernel<GlassRefractionShader> {
 public:
  static constexpr const char* Name = "GlassRefractionShader";
  static constexpr const char* VertFile = "kernels/glass_refraction.vert";
  static constexpr const char* FragFile = "kernels/glass_refraction.frag";
  static constexpr bool ServesAlphaOnly = false;

  /// Dimensions, in the bundle's dimension order:
  ///   vert: GP_KIND (0=ellipse common-color, 1=uv-matrix quad)
  ///   frag: GP_KIND (mirrored), GEOMETRY_KIND (0=SDF rounded rect, 1=SDF other, 2=UDF),
  ///         DISPERSION_ON, HAS_XP
  static constexpr Dim vertDims[] = {{"GP_KIND", 2}};
  static constexpr Dim fragDims[] = {{"GP_KIND", 2},
                                     {"GEOMETRY_KIND", 3},
                                     {"DISPERSION_ON", 2},
                                     {XP_DEFINE_NAME, XP_VALUE_COUNT}};

  static bool Accept(const DrawShape& shape);
  static Variants Select(const DrawShape& shape);
};

}  // namespace tgfx
