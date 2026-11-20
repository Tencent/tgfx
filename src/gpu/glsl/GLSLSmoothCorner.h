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
 * Emits the implicit function of a smooth-corner transition curve, the cubic Bezier curve with
 * control points B0 to B3, where B0, B1 and B2 lie on the x axis. The function has the signature:
 *   float name(vec2 q, float transitionStart, float step, float arcOffsetAlong,
 *              float arcOffsetCross);
 *     q                the point in the corner frame
 *     transitionStart  the distance from the vertex to B0 along the x axis
 *     step             the control-point spacing
 *     arcOffsetAlong   the offset from B2 to B3 along the x axis, toward the vertex
 *     arcOffsetCross   the offset from B2 to B3 along the y axis
 * It returns a value negative inside the shape and positive outside, which is not a distance.
 * @param builder the shader builder that receives the function
 * @return the mangled function name
 */
std::string EmitTransitionImplicitFunction(ShaderBuilder* builder);

/**
 * Emits the implicit function of an ellipse. The function has the signature:
 *   float name(vec2 q, vec2 radii);
 *     q      the point relative to the ellipse center
 *     radii  the ellipse semi-axes on x and y
 * It returns a value negative inside the ellipse and positive outside, which is not a distance.
 * @param builder the shader builder that receives the function
 * @return the mangled function name
 */
std::string EmitEllipseImplicitFunction(ShaderBuilder* builder);

/**
 * Emits a cubic Bezier distance function that locates the nearest point by Gauss-Newton
 * iteration. The function has the signature:
 *   float name(vec2 q, vec2 b0, vec2 b1, vec2 b2, vec2 b3);
 *     q       the point
 *     b0..b3  the control points of the cubic, running from the x-axis edge toward the y-axis edge
 * It returns the signed distance from q to the cubic, negative inside the shape. When needNormal
 * is true, it returns a vec3 instead, whose yz carries the unnormalized curve normal at the nearest
 * point.
 * @param builder the shader builder that receives the function
 * @param iterations the number of Gauss-Newton steps from each initial parameter; more steps cost
 *        more and reduce the error
 * @param needNormal whether the function also returns the nearest-point normal
 * @return the mangled function name
 */
std::string EmitCubicBezierNewtonDistFunction(ShaderBuilder* builder, int iterations,
                                              bool needNormal);

/**
 * Emits a Sampson approximate distance function for an ellipse. The function has the signature:
 *   float name(vec2 q, vec2 radii);
 *     q      the point relative to the ellipse center
 *     radii  the ellipse semi-axes on x and y
 * It returns the signed distance, negative inside, which approaches the true distance as q nears
 * the ellipse.
 * @param builder the shader builder that receives the function
 * @return the mangled function name
 */
std::string EmitEllipseSampsonDistFunction(ShaderBuilder* builder);
}  // namespace tgfx