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
#include <utility>

namespace tgfx {
class ShaderBuilder;

// Emits an implicit sign function for the smooth-corner transition cubic under the given base
// name and returns the mangled function name.
std::string EmitImplicitCubicBezierSignFunction(ShaderBuilder* builder,
                                                const std::string& name);

// Emits a Gauss-Newton cubic Bezier function with the given iteration count. The generated
// function returns the distance, optionally packed with the nearest-point normal in a vec3.
std::string EmitNewtonCubicBezierDistFunction(ShaderBuilder* builder, int iterations,
                                              bool needNormal);

// Emits an implicit ellipse sign function under the given base name and returns the mangled name.
std::string EmitImplicitEllipseSignFunction(ShaderBuilder* builder, const std::string& name);

// Emits a Sampson approximate distance function for an ellipse under the given base name and
// returns the mangled function name. The generated function returns the distance, optionally
// packed with the ellipse normal in a vec3.
std::string EmitSampsonEllipseDistFunction(ShaderBuilder* builder, const std::string& name,
                                           bool needNormal);

// Emits a signed Newton distance function for a finite elliptical arc. Angles are in radians with
// startAngle <= endAngle. The point is relative to the ellipse center. Returns the mangled name.
std::string EmitNewtonEllipticalArcDistFunction(ShaderBuilder* builder, const std::string& name,
                                                int iterations, bool needNormal);

// Emits the smoothed circular and elliptical corner functions for non-antialiased fills and returns
// their mangled names. Both evaluate the implicit equations of the corner segments, so only the
// sign of the result is meaningful: negative inside the shape and positive outside.
std::pair<std::string, std::string> EmitSmoothCornerNonAAFillFunctions(ShaderBuilder* builder);
}  // namespace tgfx
