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

namespace tgfx {
class ShaderBuilder;

/**
 * Emits the implicit function of a smooth-corner transition curve.
 * The generated function returns a value negative inside the shape and positive outside, which is
 * not a distance.
 * @param builder the shader builder that receives the function
 * @return the mangled function name
 */
std::string EmitTransitionImplicitFunction(ShaderBuilder* builder);

/**
 * Emits the implicit function of an ellipse.
 * The generated function returns a value negative inside the ellipse and positive outside, which
 * is not a distance.
 * @param builder the shader builder that receives the function
 * @return the mangled function name
 */
std::string EmitEllipseImplicitFunction(ShaderBuilder* builder);

/**
 * Emits a cubic Bezier distance function that locates the nearest point by Gauss-Newton iteration.
 * The generated function returns the signed distance, optionally packed with the nearest-point
 * normal in a vec3.
 * @param builder the shader builder that receives the function
 * @param iterations the number of Gauss-Newton steps from each initial parameter; more steps cost
 *        more and reduce the error
 * @param needNormal whether the generated function also returns the nearest-point normal
 * @return the mangled function name
 */
std::string EmitCubicBezierNewtonDistFunction(ShaderBuilder* builder, int iterations,
                                              bool needNormal);

/**
 * Emits a Sampson approximate distance function for an ellipse.
 * The generated function returns the signed distance, which is accurate to first order near the
 * ellipse.
 * @param builder the shader builder that receives the function
 * @return the mangled function name
 */
std::string EmitEllipseSampsonDistFunction(ShaderBuilder* builder);
}  // namespace tgfx