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

#include "GLSLSmoothCorner.h"
#include "gpu/ShaderBuilder.h"

namespace tgfx {

std::string EmitSmoothCornerFunctions(ShaderBuilder* builder, std::string* ellipticalEntry) {
  const auto newtonStepName = builder->getMangledFunctionName("smoothCornerNewtonStep");
  const auto bezierDistName = builder->getMangledFunctionName("smoothCornerBezierDist");
  const auto cornerDistName = builder->getMangledFunctionName("smoothCornerDist");

  // One Gauss-Newton step toward the parameter of the closest point on the cubic, given the
  // coefficients of B(t) - p in ascending order.
  builder->addFunction(("highp float " + newtonStepName + R"((highp float t, highp vec2 d0,
    highp vec2 d1, highp vec2 d2, highp vec2 d3) {
  highp vec2 a2 = d2 + t * d3;
  highp vec2 a1 = d1 + t * a2;
  highp vec2 b2 = a2 + t * d3;
  highp vec2 value = d0 + t * a1;
  highp vec2 deriv = a1 + t * b2;
  return t - dot(deriv, value) / max(dot(deriv, deriv), 1.1755e-38);
})"));

  // Signed distance from p to the cubic bezier, negative on the interior side. The contour is
  // traversed from the x-axis edge toward the y-axis edge, where the interior (first quadrant)
  // gives a negative cross product of the curve derivative and the point offset.
  builder->addFunction(("highp float " + bezierDistName + R"((highp vec2 p, highp vec2 b0,
    highp vec2 b1, highp vec2 b2, highp vec2 b3) {
  highp vec2 d3 = -b0 + 3.0 * b1 - 3.0 * b2 + b3;
  highp vec2 d2 = 3.0 * b0 - 6.0 * b1 + 3.0 * b2;
  highp vec2 d1 = -3.0 * b0 + 3.0 * b1;
  highp vec2 d0 = b0 - p;
  highp float best = 1.0e38;
  highp float bestT = 0.0;
  highp float seed = 0.0;
  for (int i = 0; i < 3; i++) {
    highp float t = seed;
    for (int j = 0; j < 2; j++) {
      t = )" + newtonStepName +
                        R"((t, d0, d1, d2, d3);
    }
    t = clamp(t, 0.0, 1.0);
    highp vec2 diff = ((d3 * t + d2) * t + d1) * t + d0;
    highp float distSq = dot(diff, diff);
    if (distSq < best) {
      best = distSq;
      bestT = t;
    }
    seed += 0.5;
  }
  highp vec2 bestDiff = ((d3 * bestT + d2) * bestT + d1) * bestT + d0;
  highp vec2 bestDeriv = (3.0 * d3 * bestT + 2.0 * d2) * bestT + d1;
  highp float side = bestDeriv.y * bestDiff.x - bestDeriv.x * bestDiff.y;
  return sqrt(best) * (side > 0.0 ? 1.0 : -1.0);
})"));

  // Signed distance to the smoothed corner, negative inside. The arc sector test compares the
  // squared cosine against cos²(sweep/2), which degenerates to "never" at full smoothness where
  // the arc vanishes.
  builder->addFunction(("highp float " + cornerDistName + R"((highp vec2 q, highp float radius,
    highp vec4 coeffs, highp float arcCosSquared) {
  highp float footprint = coeffs.x * radius;
  highp float step = coeffs.y * radius;
  highp vec2 arcCenter = vec2(radius);
  highp vec2 d = q - arcCenter;
  highp float bisectorDot = dot(d, vec2(-0.70710678));
  if (bisectorDot > 0.0 && bisectorDot * bisectorDot > arcCosSquared * dot(d, d)) {
    return length(d) - radius;
  }
  highp float ctrl2 = footprint - 3.0 * step;
  highp vec2 arcStart = vec2(ctrl2 - coeffs.z * radius, coeffs.w * radius);
  if (q.x >= q.y) {
    return )" + bezierDistName +
                        R"((q, vec2(footprint, 0.0),
        vec2(footprint - 2.0 * step, 0.0), vec2(ctrl2, 0.0), arcStart);
  }
  highp vec2 arcEnd = arcStart.yx;
  return )" + bezierDistName +
                        R"((q, arcEnd, vec2(0.0, ctrl2),
      vec2(0.0, footprint - 2.0 * step), vec2(0.0, footprint));
})"));

  if (ellipticalEntry != nullptr) {
    const auto ellipticalName = builder->getMangledFunctionName("smoothCornerDistElliptical");
    // Elliptical corner: dispatch in the circular frame (y scaled by 1/ky), evaluate the
    // distance in device space on the scaled geometry.
    builder->addFunction(("highp float " + ellipticalName + R"((highp vec2 q, highp vec2 radii,
      highp vec4 coeffs, highp float arcCosSquared) {
  highp float rx = radii.x;
  highp float ky = radii.y / radii.x;
  highp float footprint = coeffs.x * rx;
  highp float step = coeffs.y * rx;
  highp vec2 qc = vec2(q.x, q.y / ky);
  highp vec2 dc = qc - vec2(rx);
  highp float bisectorDot = dot(dc, vec2(-0.70710678));
  if (bisectorDot > 0.0 && bisectorDot * bisectorDot > arcCosSquared * dot(dc, dc)) {
    // Arc sector: Sampson distance to the corner ellipse, evaluated in device space. The arc
    // center is (rx, ry), i.e. radii itself.
    highp vec2 offset = (q - radii) / radii;
    highp float test = dot(offset, offset) - 1.0;
    highp vec2 grad = 2.0 * offset / radii;
    return test * inversesqrt(max(dot(grad, grad), 1.1755e-38));
  }
  highp float ctrl2 = footprint - 3.0 * step;
  highp vec2 arcStart = vec2(ctrl2 - coeffs.z * rx, coeffs.w * rx);
  // The control points are built in the circular frame and scaled by ky into device space, so
  // the Newton solver measures device-space distance on the exact scaled cubic.
  if (qc.x >= qc.y) {
    return )" + bezierDistName +
                          R"((q, vec2(footprint, 0.0), vec2(footprint - 2.0 * step, 0.0),
        vec2(ctrl2, 0.0), vec2(arcStart.x, arcStart.y * ky));
  }
  return )" + bezierDistName +
                          R"((q, vec2(arcStart.y, arcStart.x * ky), vec2(0.0, ctrl2 * ky),
      vec2(0.0, (footprint - 2.0 * step) * ky), vec2(0.0, footprint * ky));
})"));
    *ellipticalEntry = ellipticalName;
  }

  return cornerDistName;
}

}  // namespace tgfx
