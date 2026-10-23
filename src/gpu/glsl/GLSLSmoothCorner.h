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

// Emits the smooth-corner helper functions into the builder and returns the mangled name of the
// entry function, whose emitted signature is:
//
//   float smoothCornerDist(highp vec2 q, highp float radius, highp vec4 coeffs,
//                          highp float arcCosSquared)
//
// q is in the corner frame: the corner vertex is the origin, the two edges run along the +x and
// +y axes, and the shape interior lies in the first quadrant. radius is the circular corner
// radius. coeffs packs the per-unit-radius coefficients (tangentRatio, controlStep, endOffsetX,
// endOffsetY) from ComputeSmoothCornerCoeffs, and arcCosSquared is its arcCosSquared member. The
// returned distance is negative inside the corner contour.
//
// When ellipticalEntry is not null, an elliptical-corner variant is emitted as well and its
// mangled name is written to ellipticalEntry:
//
//   float smoothCornerDistElliptical(highp vec2 q, highp vec2 radii, highp vec4 coeffs,
//                                    highp float arcCosSquared)
//
// radii are the per-axis corner radii (rx, ry). Territory dispatch (arc sector test and the
// diagonal split) happens in the circular frame, i.e. with y divided by ky = ry/rx, while the
// distance itself is evaluated in device space: the arc sector uses the Sampson distance to the
// corner ellipse, and the transition beziers are built in the circular frame then scaled by ky
// so the Newton solver works on the exact scaled cubic.
// Call at most once per builder.
std::string EmitSmoothCornerFunctions(ShaderBuilder* builder, std::string* ellipticalEntry);
}  // namespace tgfx
