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
 * AtlasTextFillShader: AtlasTextOp's glyph processor, at most one coverage FP. A single coverage
 * FP is either a direct analytic clip (runtime clip contract uniforms) or an alpha-only affine
 * device-space mask whose sampler facts come off the IR's DeviceMask term — the acceptance reads
 * the IR, so it is defined in the family's .cpp.
 */
class AtlasTextFillShader : public FillKernel<AtlasTextFillShader> {
 public:
  static constexpr const char* Name = "AtlasTextFillShader";
  static constexpr const char* VertFile = "kernels/atlas_text_fill.vert";
  static constexpr const char* FragFile = "kernels/atlas_text_fill.frag";
  static constexpr bool ServesAlphaOnly = false;

  static constexpr Dim vertDims[] = {{"HAS_COVERAGE", 2}};
  static constexpr Dim fragDims[] = {{"HAS_COVERAGE", 2}, {XP_DEFINE_NAME, XP_VALUE_COUNT}};

  static bool Accept(const DrawShape& shape);
  static Variants Select(const DrawShape& shape);
};

}  // namespace tgfx
