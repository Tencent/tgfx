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
std::string EmitTransitionImplicitFunction(ShaderBuilder* builder) {
  // Evaluates the implicit equation of a transition curve, negative inside the shape.
  // The result is not a distance, and its sign is meaningful only in the region where the caller
  // has selected this curve.
  // The curve is the cubic Bezier with control points:
  //   b0 = (transitionStart, 0)
  //   b1 = (transitionStart - 2*step, 0)
  //   b2 = (transitionStart - 3*step, 0)
  //   b3 = (transitionStart - 3*step - arcOffsetX, arcOffsetY)
  // Its parametric form is
  //   X(t) = transitionStart - 6*step*t + 3*step*t^2 - arcOffsetX*t^3
  //   Y(t) = arcOffsetY*t^3
  // With
  //   a = y / arcOffsetY
  //   b = (x - transitionStart + arcOffsetX*y / arcOffsetY) / (3*step)
  // the implicit equation is
  //   f(q) = a^2 - 6*b*a - 8*a - b^3
  //   q                the point in the corner frame
  //   transitionStart  the distance from the vertex to the curve start along the x axis
  //   step             the control-point spacing
  //   arcOffsetX       the offset from b2 to the curve end along the x axis
  //   arcOffsetY       the offset from b2 to the curve end along the y axis
  //   return           a value negative inside the shape and positive outside
  const auto implicitFunc = builder->getMangledFunctionName("smoothCornerTransitionImplicit");
  // clang-format off
  builder->addFunction(R"(
highp float )" + implicitFunc + R"((highp vec2 q, highp float transitionStart, highp float step,
                                    highp float arcOffsetX, highp float arcOffsetY) {
  highp float a = q.y / arcOffsetY;
  highp float b = (q.x - transitionStart + arcOffsetX * q.y / arcOffsetY) / (3.0 * step);
  return a * a - 6.0 * b * a - 8.0 * a - b * b * b;
}
)");
  // clang-format on
  return implicitFunc;
}

std::string EmitEllipseImplicitFunction(ShaderBuilder* builder) {
  // Evaluates the implicit ellipse equation, negative inside the ellipse.
  // The result is not a distance, and only its sign is meaningful. The implicit equation is
  //   f(q) = (x / radii.x)^2 + (y / radii.y)^2 - 1
  //   q       the point relative to the ellipse center
  //   radii   the ellipse semi-axes on x and y
  //   return  a value negative inside the ellipse and positive outside
  const auto implicitFunc = builder->getMangledFunctionName("smoothCornerEllipseImplicit");
  // clang-format off
  builder->addFunction(R"(
highp float )" + implicitFunc + R"((highp vec2 q, highp vec2 radii) {
  highp vec2 safeRadii = max(radii, vec2(1e-6));
  highp vec2 offset = q / safeRadii;
  return dot(offset, offset) - 1.0;
}
)");
  // clang-format on
  return implicitFunc;
}

std::string EmitCubicBezierNewtonDistFunction(ShaderBuilder* builder, int iterations,
                                              bool needNormal) {
  // Performs one Gauss-Newton step toward the parameter of the nearest point on the cubic.
  // For the cubic
  //   B(t) = (1-t)^3*b0 + 3(1-t)^2*t*b1 + 3(1-t)*t^2*b2 + t^3*b3
  // the step is
  //   t' = t - dot(B(t) - q, B'(t)) / dot(B'(t), B'(t))
  // Expanded in powers of t,
  //   B(t) - q = d0 + d1*t + d2*t^2 + d3*t^3
  // offset is B(t) - q and deriv is B'(t), both evaluated with Horner's scheme.
  //   t       the current curve parameter
  //   d0..d3  the coefficients of B(t) - q for t^0 through t^3
  //   return  the updated curve parameter, not clamped to [0, 1]
  const auto newtonStepFunc = builder->getMangledFunctionName("smoothCornerBezierNewtonStep");
  // clang-format off
  builder->addFunction(R"(
highp float )" + newtonStepFunc + R"((highp float t, highp vec2 d0, highp vec2 d1, highp vec2 d2,
                                      highp vec2 d3) {
  highp vec2 a2 = d2 + t * d3;
  highp vec2 a1 = d1 + t * a2;
  highp vec2 b2 = a2 + t * d3;
  highp vec2 offset = d0 + t * a1;
  highp vec2 deriv = a1 + t * b2;
  return t - dot(deriv, offset) / max(dot(deriv, deriv), 1.1755e-38);
}
)");
  // clang-format on

  // When needNormal is true, the result is vec3(distance, normal.x, normal.y), where normal is
  // the unnormalized curve normal at the nearest point.
  const std::string returnType = needNormal ? "highp vec3 " : "highp float ";
  const auto returnValue = needNormal ? "vec3(sqrt(nearestDistSq) * (side > 0.0 ? 1.0 : -1.0), "
                                        "vec2(nearestDeriv.y, -nearestDeriv.x))"
                                      : "sqrt(nearestDistSq) * (side > 0.0 ? 1.0 : -1.0)";
  // Computes the signed distance from q to the cubic over t in [0, 1], negative inside the shape.
  // The cubic and its expansion in powers of t are
  //   B(t) = (1-t)^3*b0 + 3(1-t)^2*t*b1 + 3(1-t)*t^2*b2 + t^3*b3
  //   B(t) - q = d0 + d1*t + d2*t^2 + d3*t^3
  // Each of the seeds 0, 0.5 and 1 runs the Gauss-Newton steps and is clamped to [0, 1] afterwards.
  // The endpoints are not compared on their own, so the caller ensures that the nearest point to q
  // is not an endpoint. With t* the candidate nearest to q, the result is
  //   d = |B(t*) - q| * sign(cross(B'(t*), q - B(t*)))
  // B'(t*) is the tangent at the nearest point, so its cross product with the offset q - B(t*)
  // tells which side of the tangent q lies on. The contour runs from the x-axis edge toward the
  // y-axis edge, so the cross product is negative inside the shape. The normal is
  //   n = (B'(t*).y, -B'(t*).x)
  //   q       the point
  //   b0..b3  the control points of the cubic
  //   return  the signed distance; when needNormal is true, a vec3 that also carries the
  //           unnormalized curve normal at the nearest point in yz
  const auto distFunc = builder->getMangledFunctionName("smoothCornerBezierNewtonDist");
  // clang-format off
  builder->addFunction(
returnType + distFunc + R"((highp vec2 q, highp vec2 b0, highp vec2 b1, highp vec2 b2,
                            highp vec2 b3) {
  highp vec2 d3 = -b0 + 3.0 * b1 - 3.0 * b2 + b3;
  highp vec2 d2 = 3.0 * b0 - 6.0 * b1 + 3.0 * b2;
  highp vec2 d1 = -3.0 * b0 + 3.0 * b1;
  highp vec2 d0 = b0 - q;
  highp float nearestDistSq = 1.0e38;
  highp float nearestT = 0.0;
  highp float seed = 0.0;
  for (int i = 0; i < 3; i++) {
    highp float t = seed;
    for (int j = 0; j < )" + std::to_string(iterations) + R"(; j++) {
      t = )" + newtonStepFunc + R"((t, d0, d1, d2, d3);
    }
    t = clamp(t, 0.0, 1.0);
    highp vec2 diff = ((d3 * t + d2) * t + d1) * t + d0;
    highp float distSq = dot(diff, diff);
    if (distSq < nearestDistSq) {
      nearestDistSq = distSq;
      nearestT = t;
    }
    seed += 0.5;
  }
  highp vec2 nearestDiff = ((d3 * nearestT + d2) * nearestT + d1) * nearestT + d0;
  highp vec2 nearestDeriv = (3.0 * d3 * nearestT + 2.0 * d2) * nearestT + d1;
  highp float side = nearestDeriv.y * nearestDiff.x - nearestDeriv.x * nearestDiff.y;
  return )" + returnValue + R"(;
}
)");
  // clang-format on
  return distFunc;
}

std::string EmitEllipseSampsonDistFunction(ShaderBuilder* builder) {
  // Computes the approximate signed distance to an ellipse, negative inside the ellipse.
  // The Sampson distance is accurate to first order near the ellipse. With
  //   f(q) = (x / radii.x)^2 + (y / radii.y)^2 - 1
  //   grad f(q) = (2x / radii.x^2, 2y / radii.y^2)
  // the displacement delta that moves q onto the ellipse satisfies f(q + delta) = 0. Keeping only
  // the first-order Taylor term gives
  //   f(q) + dot(grad f(q), delta) = 0
  // The shortest such delta is parallel to the normal, delta = lambda * grad f(q), so
  //   d = f(q) / |grad f(q)|
  //   q       the point relative to the ellipse center
  //   radii   the ellipse semi-axes on x and y
  //   return  the approximate signed distance
  const auto distFunc = builder->getMangledFunctionName("smoothCornerEllipseSampsonDist");
  // clang-format off
  builder->addFunction(R"(
highp float )" + distFunc + R"((highp vec2 q, highp vec2 radii) {
  highp vec2 safeRadii = max(radii, vec2(1e-6));
  highp vec2 offset = q / safeRadii;
  highp float fq = dot(offset, offset) - 1.0;
  highp vec2 grad = 2.0 * offset / safeRadii;
  return fq / max(length(grad), 1e-20);
}
)");
  // clang-format on
  return distFunc;
}
}  // namespace tgfx