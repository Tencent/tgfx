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
//                          highp vec3 arcParams)
//
// q is in the corner frame: the corner vertex is the origin, the two edges run along the +x and
// +y axes, and the shape interior lies in the first quadrant. radius is the circular corner
// radius. coeffs packs the per-unit-radius coefficients (tangentRatio, controlStep, endOffsetX,
// endOffsetY) from ComputeSmoothCornerCoeffs, and arcParams packs (turnSin, turnCos, arcHandle).
// Segment selection uses the true join-point normals in device space (the fs/fe sign table), and
// the retained arc uses the exact circle distance. The returned distance is negative inside the
// corner contour.
//
// When ellipticalEntry is not null, an elliptical-corner variant is emitted as well and its
// mangled name is written to ellipticalEntry:
//
//   float smoothCornerDistElliptical(highp vec2 q, highp vec2 radii, highp vec4 coeffs,
//                                    highp vec3 arcParams)
//
// radii are the per-axis corner radii (rx, ry). The retained arc is evaluated as a single cubic
// (the ky-scaled circular-arc cubic, ky = ry/rx) instead of a trigonometric ellipse distance, so
// no per-fragment trigonometry is needed.
//
// When stroke is true, the cubic distance solver iterates three times per seed instead of two:
// the stroke coverage band is much wider than the fill's, and two iterations were measured to
// exceed the 0.25px distance target within the supported envelope.
//
// When localStroke is true (stroke under a non-uniformly scaling matrix), the entry functions
// evaluate distances in the unscaled local frame and take an extra trailing "out vec2 normal"
// parameter that receives the contour normal at the nearest point (unnormalized; zero only for
// degenerate queries). The caller converts local distances to device pixels per fragment via the
// inverse-transpose matrix.
// Call at most once per builder.
std::string EmitSmoothCornerFunctions(ShaderBuilder* builder, std::string* ellipticalEntry,
                                      bool stroke, bool localStroke);
}  // namespace tgfx
