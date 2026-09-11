/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
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

#include "gpu/FragmentShaderBuilder.h"

namespace tgfx {

/**
 * Number of outer quadrature points used by the rounded-rect closed form. Anisotropic sigma scales
 * the shape's effective aspect ratio, which raises the quadrature error, so 8 is required to stay
 * near one 8-bit level; 4 would reach 3.55 levels in the worst anisotropic case.
 */
static constexpr int ShapeBlurQuadratureCount = 8;

/**
 * Emits the shared helpers used by the analytic shadow processors. Safe to call from several
 * processors within one program: addFunction() de-duplicates identical definitions.
 */
void AppendShapeBlurFunctions(FragmentShaderBuilder* fragBuilder);

/**
 * Emits the shared helpers plus the rounded-rect quadrature. Callers that only need the rectangle
 * form should use AppendShapeBlurFunctions() to keep the shader smaller.
 */
void AppendRoundRectBlurFunctions(FragmentShaderBuilder* fragBuilder);

/**
 * Emits the shape mask helpers used by the inner shadow processors: a rectangle SDF and its
 * coverage conversion. These are evaluated in content pixels rather than sigma-normalized space,
 * because the mask's antialiased edge is one pixel wide regardless of sigma.
 */
void AppendShapeMaskFunctions(FragmentShaderBuilder* fragBuilder);

/**
 * Emits the shape mask helpers plus the rounded-rect SDF.
 */
void AppendRoundRectMaskFunctions(FragmentShaderBuilder* fragBuilder);

}  // namespace tgfx
