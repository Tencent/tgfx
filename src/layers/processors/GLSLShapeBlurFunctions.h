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

#include <string>
#include "gpu/FragmentShaderBuilder.h"
#include "tgfx/core/Point.h"

namespace tgfx {

/**
 * Each emitting function below may be called at most once per fragment processor, and the
 * rectangle and rounded-rect variants of a group are mutually exclusive within one processor:
 * AppendRectBlurFunctions with AppendRRectBlurFunctions, and AppendRectMaskFunctions with
 * AppendRRectMaskFunctions.
 */

/**
 * Emits the rectangle blur coverage function and returns its name. The function has the
 * signature:
 *   float name(vec2 coord, vec2 halfSize);
 */
std::string AppendRectBlurFunctions(FragmentShaderBuilder* fragBuilder);

/**
 * Returns the number of quadrature points the rounded-rect coverage form should use for a corner
 * whose two semi-axes are given in sigma units.
 */
int GetRRectBlurQuadratureCount(const Point& cornerOverSigma);

/**
 * Emits the rounded-rect blur coverage function and returns its name. The function has the
 * signature:
 *   float name(vec2 coord, vec2 halfSize, vec2 corner);
 * @param quadratureCount the number of quadrature points the coverage form uses along the axis it
 * does not solve in closed form. GetRRectBlurQuadratureCount gives the value to pass.
 */
std::string AppendRRectBlurFunctions(FragmentShaderBuilder* fragBuilder, int quadratureCount);

/**
 * Names of the mask functions emitted by AppendRectMaskFunctions and AppendRRectMaskFunctions.
 */
struct ShapeMaskFunctionNames {
  /**
   * The signed distance function. For a rectangle it has the signature:
   *   float sdf(vec2 p, vec2 halfSize);
   * For a rounded rect it has the signature:
   *   float sdf(vec2 p, vec2 halfSize, vec2 corner);
   */
  std::string sdf;
  /**
   * The function that converts a signed distance to coverage. It has the signature:
   *   float coverage(float sdf);
   */
  std::string coverage;
};

/**
 * Emits the rectangle mask functions and returns their names.
 */
ShapeMaskFunctionNames AppendRectMaskFunctions(FragmentShaderBuilder* fragBuilder);

/**
 * Emits the rounded-rect mask functions and returns their names.
 */
ShapeMaskFunctionNames AppendRRectMaskFunctions(FragmentShaderBuilder* fragBuilder);

}  // namespace tgfx
