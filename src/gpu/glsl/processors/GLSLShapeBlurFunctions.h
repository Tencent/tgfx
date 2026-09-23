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
#include "tgfx/core/Point.h"

namespace tgfx {

/**
 * Each function below may be called at most once per FragmentShaderBuilder, and the rectangle and
 * rounded-rect variants of a group are mutually exclusive on one builder: AppendRectBlurFunctions
 * with AppendRRectBlurFunctions, and AppendRectMaskFunctions with AppendRRectMaskFunctions.
 */

/**
 * Emits:
 *   float shapeBlurRectCoverage(vec2 coord, vec2 halfSize);
 */
void AppendRectBlurFunctions(FragmentShaderBuilder* fragBuilder);

/**
 * Returns the number of sample points the rounded-rect coverage form should use for a corner whose
 * two semi-axes are given in sigma units. A corner with no arc leaves the row half-width constant
 * along the axis being summed, which the midpoint rule integrates exactly, so it takes the low
 * count.
 */
int GetRRectBlurQuadratureCount(const Point& cornerOverSigma);

/**
 * Emits:
 *   float shapeBlurRRectCoverage(vec2 coord, vec2 halfSize, vec2 corner);
 * @param quadratureCount the number of sample points the coverage form uses along the axis it does
 * not solve in closed form. GetRRectBlurQuadratureCount gives the value to pass.
 */
void AppendRRectBlurFunctions(FragmentShaderBuilder* fragBuilder, int quadratureCount);

/**
 * Emits:
 *   float shapeMaskRectSDF(vec2 p, vec2 halfSize);
 *   float shapeMaskCoverage(float sdf);
 */
void AppendRectMaskFunctions(FragmentShaderBuilder* fragBuilder);

/**
 * Emits:
 *   float shapeMaskRRectSDF(vec2 p, vec2 halfSize, vec2 corner);
 *   float shapeMaskRectSDF(vec2 p, vec2 halfSize);
 *   float shapeMaskCoverage(float sdf);
 */
void AppendRRectMaskFunctions(FragmentShaderBuilder* fragBuilder);

}  // namespace tgfx
