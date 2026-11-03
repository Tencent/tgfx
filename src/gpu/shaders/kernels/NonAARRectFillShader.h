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
 * NonAARRectFillShader: RRectDrawOp's non-AA form. The solid variant takes at most one sole
 * analytic clip (runtime clip contract uniforms); the textured variant carries the single-tap
 * tiled modes only and never combines with stroke — the `allowed` predicate keeps that
 * combination out of the bundle. The tiled leaf's recipe comes off the carried IR, so the
 * acceptance is defined in the family's .cpp.
 */
class NonAARRectFillShader : public FillKernel<NonAARRectFillShader> {
 public:
  static constexpr const char* Name = "NonAARRectFillShader";
  static constexpr const char* VertFile = "kernels/non_aa_rrect_fill.vert";
  static constexpr const char* FragFile = "kernels/non_aa_rrect_fill.frag";
  static constexpr bool ServesAlphaOnly = false;

  static constexpr Dim vertDims[] = {{"STROKE", 2}, {"TEXTURED", 2}};
  static constexpr Dim fragDims[] = {{"STROKE", 2},
                                     {"TEXTURED", 2},
                                     {XP_DEFINE_NAME, XP_VALUE_COUNT}};

  static bool Accept(const DrawShape& shape);
  static Variants Select(const DrawShape& shape);

  /// The textured variant is fill-only: a stroked rect never carries the tiled texture child,
  /// so the combination stays out of the bundle (mirrored to both stages, so the fragment view
  /// sees it).
  static bool allowed(const Values& values) {
    return !(values["TEXTURED"] != 0 && values["STROKE"] != 0);
  }
};

}  // namespace tgfx
