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

std::string EmitImplicitCubicBezierSignFunction(ShaderBuilder* builder,
                                                const std::string& name) {
  // Evaluates the implicit form of the smooth-corner transition cubic.
  //   q           the point in the corner frame
  //   footprint   the distance from the corner vertex to where the transition leaves the edge
  //   step        the control-point step along the edge
  //   arcOffsetX  the offset of the arc start from the tangent intersection, along the edge
  //   arcOffsetY  the height of the arc start above the edge
  //   return      a value negative inside the shape and positive outside, not a distance
  //
  // The caller selects the segment first, so only the sign in that segment's region is needed.
  const auto signFunc = builder->getMangledFunctionName(name.c_str());
  // clang-format off
  builder->addFunction(R"(
highp float )" + signFunc + R"((highp vec2 q, highp float footprint, highp float step,
                                highp float arcOffsetX, highp float arcOffsetY) {
  highp float nu = q.y / arcOffsetY;
  highp float beta = (q.x - footprint + arcOffsetX * q.y / arcOffsetY) / (3.0 * step);
  return nu * nu - 6.0 * beta * nu - 8.0 * nu - beta * beta * beta;
}
)");
  // clang-format on
  return signFunc;
}

std::string EmitNewtonCubicBezierDistFunction(ShaderBuilder* builder, int iterations,
                                              bool needNormal) {
  const auto newtonStepFunc = builder->getMangledFunctionName("smoothCornerBezierDistStep");
  const auto distFunc = builder->getMangledFunctionName("smoothCornerBezierDist");
  const auto returnType = needNormal ? "highp vec3 " : "highp float ";
  const auto returnValue =
      needNormal
          ? "vec3(sqrt(best) * (side > 0.0 ? 1.0 : -1.0), vec2(bestDeriv.y, -bestDeriv.x))"
          : "sqrt(best) * (side > 0.0 ? 1.0 : -1.0)";

  // One Gauss-Newton step toward the parameter of the closest point on the cubic, given the
  // coefficients of B(t) - p in ascending order.
  //   t       the current curve parameter
  //   d0..d3  the coefficients of B(t) - p for t^0 through t^3
  //   return  the updated curve parameter, not clamped to [0, 1]
  // clang-format off
  builder->addFunction(R"(
highp float )" + newtonStepFunc + R"((highp float t, highp vec2 d0, highp vec2 d1, highp vec2 d2,
                                      highp vec2 d3) {
  highp vec2 a2 = d2 + t * d3;
  highp vec2 a1 = d1 + t * a2;
  highp vec2 b2 = a2 + t * d3;
  highp vec2 value = d0 + t * a1;
  highp vec2 deriv = a1 + t * b2;
  return t - dot(deriv, value) / max(dot(deriv, deriv), 1.1755e-38);
}
)");
  // clang-format on

  // Returns the signed distance from p to the cubic over t in [0, 1].
  //   p       the point
  //   b0..b3  the control points of the cubic
  //   return  the distance, negative inside the shape; when needNormal is true, a vec3 that also
  //           carries the unnormalized curve normal at the nearest point in yz
  //
  // The contour runs from the x-axis edge toward the y-axis edge, so the interior gives a negative
  // cross product of the curve derivative and the point offset.
  // clang-format off
  builder->addFunction(
returnType + distFunc + R"((highp vec2 p, highp vec2 b0, highp vec2 b1,
                            highp vec2 b2, highp vec2 b3) {
  highp vec2 d3 = -b0 + 3.0 * b1 - 3.0 * b2 + b3;
  highp vec2 d2 = 3.0 * b0 - 6.0 * b1 + 3.0 * b2;
  highp vec2 d1 = -3.0 * b0 + 3.0 * b1;
  highp vec2 d0 = b0 - p;
  // Endpoints are independent candidates: a seed sitting at 0 or 1 does not stay there after
  // iterating. Non-finite candidates lose every comparison and never update the minimum.
  highp float best = dot(d0, d0);
  highp float bestT = 0.0;
  highp vec2 endDiff = d0 + d1 + d2 + d3;
  highp float endDistSq = dot(endDiff, endDiff);
  if (endDistSq < best) {
    best = endDistSq;
    bestT = 1.0;
  }
  highp float seed = 0.0;
  for (int i = 0; i < 3; i++) {
    highp float t = seed;
    for (int j = 0; j < )" + std::to_string(iterations) + R"(; j++) {
      t = )" + newtonStepFunc + R"((t, d0, d1, d2, d3);
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
  return )" + returnValue + R"(;
}
)");
  // clang-format on
  return distFunc;
}

std::string EmitImplicitEllipseSignFunction(ShaderBuilder* builder, const std::string& name) {
  // Evaluates the implicit ellipse equation.
  //   p       the point relative to the ellipse center
  //   radii   the ellipse semi-axes on x and y
  //   return  dot(p / radii, p / radii) - 1, negative inside the ellipse
  const auto signFunc = builder->getMangledFunctionName(name.c_str());
  // clang-format off
  builder->addFunction(R"(
highp float )" + signFunc + R"((highp vec2 p, highp vec2 radii) {
  highp vec2 safeRadii = max(radii, vec2(1e-6));
  highp vec2 offset = p / safeRadii;
  return dot(offset, offset) - 1.0;
}
)");
  // clang-format on
  return signFunc;
}

std::string EmitSampsonEllipseDistFunction(ShaderBuilder* builder, const std::string& name,
                                           bool needNormal) {
  const auto returnType = needNormal ? "highp vec3 " : "highp float ";
  const auto returnValue = needNormal ? "vec3(dist, grad)" : "dist";

  // Approximates the signed distance to an ellipse with the Sampson distance F / length(grad F).
  //   p       the point relative to the ellipse center
  //   radii   the ellipse semi-axes on x and y
  //   return  the approximate distance, negative inside the ellipse; when needNormal is true, a
  //           vec3 that also carries the unnormalized ellipse normal at p in yz
  const auto distFunc = builder->getMangledFunctionName(name.c_str());
  // clang-format off
  builder->addFunction(
returnType + distFunc + R"((highp vec2 p, highp vec2 radii) {
  highp vec2 safeRadii = max(radii, vec2(1e-6));
  highp vec2 offset = p / safeRadii;
  highp float test = dot(offset, offset) - 1.0;
  highp vec2 grad = 2.0 * offset / safeRadii;
  highp float dist = test / max(length(grad), 1e-20);
  return )" + returnValue + R"(;
}
)");
  // clang-format on
  return distFunc;
}

std::string EmitNewtonEllipticalArcDistFunction(ShaderBuilder* builder, const std::string& name,
                                                int iterations, bool needNormal) {
  const auto distFunc = builder->getMangledFunctionName(name.c_str());
  const auto returnType = needNormal ? "highp vec3 " : "highp float ";
  const auto returnValue = needNormal ? "vec3(sqrt(best) * (side > 0.0 ? 1.0 : -1.0), bestNormal)"
                                      : "sqrt(best) * (side > 0.0 ? 1.0 : -1.0)";

  // Returns the signed distance to a finite elliptical arc, iterating Newton steps on the
  // parametric angle.
  //   p           the point relative to the ellipse center
  //   radii       the ellipse semi-axes on x and y
  //   startAngle  the parametric angle where the arc starts, in radians
  //   endAngle    the parametric angle where the arc ends, in radians, no less than startAngle
  //   return      the distance, negative inside the ellipse; when needNormal is true, a vec3 that
  //               also carries the unnormalized ellipse normal at the nearest point in yz
  // clang-format off
  builder->addFunction(
returnType + distFunc + R"((highp vec2 p, highp vec2 radii, highp float startAngle,
                            highp float endAngle) {
  highp vec2 safeRadii = max(radii, vec2(1e-6));
  highp vec2 startPoint = safeRadii * vec2(cos(startAngle), sin(startAngle));
  highp vec2 bestDiff = p - startPoint;
  highp float best = dot(bestDiff, bestDiff);
  highp float bestT = startAngle;
  highp vec2 endPoint = safeRadii * vec2(cos(endAngle), sin(endAngle));
  highp vec2 endDiff = p - endPoint;
  highp float endDistSq = dot(endDiff, endDiff);
  if (endDistSq < best) {
    best = endDistSq;
    bestT = endAngle;
  }
  for (int i = 0; i < 4; i++) {
    highp float t = mix(startAngle, endAngle, float(i) / 3.0);
    for (int j = 0; j < )" + std::to_string(iterations) + R"(; j++) {
      highp vec2 point = safeRadii * vec2(cos(t), sin(t));
      highp vec2 deriv = safeRadii * vec2(-sin(t), cos(t));
      highp vec2 secondDeriv = -point;
      highp vec2 diff = point - p;
      highp float first = dot(deriv, diff);
      highp float second = dot(secondDeriv, diff) + dot(deriv, deriv);
      highp float safeSecond = second < 0.0 ? min(second, -1e-20) : max(second, 1e-20);
      t = clamp(t - first / safeSecond, startAngle, endAngle);
    }
    highp vec2 point = safeRadii * vec2(cos(t), sin(t));
    highp vec2 diff = p - point;
    highp float distSq = dot(diff, diff);
    if (distSq < best) {
      best = distSq;
      bestT = t;
    }
  }
  highp vec2 bestPoint = safeRadii * vec2(cos(bestT), sin(bestT));
  bestDiff = p - bestPoint;
  highp vec2 bestNormal = vec2(cos(bestT), sin(bestT)) / safeRadii;
  highp float side = dot(bestDiff, bestNormal);
  return )" + returnValue + R"(;
}
)");
  // clang-format on
  return distFunc;
}

std::pair<std::string, std::string> EmitSmoothCornerNonAAFillFunctions(ShaderBuilder* builder) {
  const auto transitionSignFunc =
      EmitImplicitCubicBezierSignFunction(builder, "smoothCornerTransitionSign");
  const auto arcSignFunc = EmitImplicitEllipseSignFunction(builder, "smoothCornerArcSign");

  // Classifies a point against a smoothed circular corner by sign alone, without computing a
  // distance.
  //   q           the point in the corner frame, with the vertex at the origin and the interior
  //               in the first quadrant
  //   radius      the corner radius
  //   coeffs      the per-unit-radius transition coefficients (footprint, step, arcOffsetX,
  //               arcOffsetY)
  //   smoothness  the corner smoothness
  //   return      a value negative inside the shape and positive outside, not a distance
  //
  // Segment selection matches the distance path, including the q.y < iy check on S2, where iy is
  // the y coordinate of the intersection of both normals (the radius here); the retained
  // arc uses the circle or ellipse implicit equation. S4 lies inside the shape beyond the
  // intersection of both normals, but near full smoothness rounding can also put fragments outside
  // ps into it, so its sign comes from the side of the inward normal at ps.
  const auto cornerSignFunc = builder->getMangledFunctionName("smoothCornerSign");
  // clang-format off
  builder->addFunction(R"(
highp float )" + cornerSignFunc + R"((highp vec2 q, highp float radius, highp vec4 coeffs,
                                      highp float smoothness) {
  highp float clampedSmoothness = clamp(smoothness, 0.0, 1.0);
  highp float turnAngle = 3.14159265359 * 0.25 * clampedSmoothness;
  highp float turnSin = sin(turnAngle);
  highp float turnCos = cos(turnAngle);
  highp float footprint = coeffs.x * radius;
  highp float step = coeffs.y * radius;
  highp float ctrl2 = footprint - 3.0 * step;
  highp vec2 ps = vec2(ctrl2 - coeffs.z * radius, coeffs.w * radius);
  highp vec2 pe = ps.yx;
  highp float fs = turnCos * (q.x - ps.x) - turnSin * (q.y - ps.y);
  highp float fe = turnSin * (q.x - pe.x) - turnCos * (q.y - pe.y);
  if (fs > 0.0) {
    if (fe > 0.0) {
      // S1
      return )" + transitionSignFunc + R"((q, footprint, step, coeffs.z * radius,
                                           coeffs.w * radius);
    } else {
      // S4
      return -dot(q - ps, vec2(turnSin, turnCos));
    }
  } else {
    if (fe > 0.0 && q.y < radius) {
      // S2
      return )" + arcSignFunc + R"((q - vec2(radius), vec2(radius));
    } else {
      // S3
      return )" + transitionSignFunc + R"((q.yx, footprint, step, coeffs.w * radius,
                                           coeffs.z * radius);
    }
  }
}
)");
  // clang-format on

  // The elliptical counterpart of the circular sign function above.
  //   q           the point in the corner frame
  //   radii       the corner radii on x and y
  //   coeffs      the per-unit-radius transition coefficients (footprint, step, arcOffsetX,
  //               arcOffsetY)
  //   smoothness  the corner smoothness
  //   return      a value negative inside the shape and positive outside, not a distance
  const auto ellipseSignFunc = builder->getMangledFunctionName("smoothCornerSignEllipse");
  // clang-format off
  builder->addFunction(R"(
highp float )" + ellipseSignFunc + R"((highp vec2 q, highp vec2 radii, highp vec4 coeffs,
                                       highp float smoothness) {
  highp float clampedSmoothness = clamp(smoothness, 0.0, 1.0);
  highp float turnAngle = 3.14159265359 * 0.25 * clampedSmoothness;
  highp float turnSin = sin(turnAngle);
  highp float turnCos = cos(turnAngle);
  highp float rx = radii.x;
  highp float ry = radii.y;
  highp float ky = ry / rx;
  highp float footprint = coeffs.x * rx;
  highp float step = coeffs.y * rx;
  highp float ctrl2 = footprint - 3.0 * step;
  highp float arcOffsetX = coeffs.z * rx;
  highp float arcOffsetY = coeffs.w * ry;
  highp vec2 ps = vec2(ctrl2 - arcOffsetX, arcOffsetY);
  highp vec2 pe = vec2(ps.y / ky, ps.x * ky);
  highp float fs = rx * turnCos * (q.x - ps.x) - ry * turnSin * (q.y - ps.y);
  highp float fe = rx * turnSin * (q.x - pe.x) - ry * turnCos * (q.y - pe.y);
  highp float iy = ry + (rx * rx - ry * ry) * turnSin * turnCos / ((turnSin + turnCos) * ry);
  if (fs > 0.0) {
    if (fe > 0.0) {
      // S1
      return )" + transitionSignFunc + R"((q, footprint, step, arcOffsetX, arcOffsetY);
    } else {
      // S4
      return -dot(q - ps, vec2(ry * turnSin, rx * turnCos));
    }
  } else {
    if (fe > 0.0 && q.y < iy) {
      // S2
      return )" + arcSignFunc + R"((q - radii, radii);
    } else {
      // S3
      return )" + transitionSignFunc + R"((q.yx, footprint * ky, step * ky,
                                           arcOffsetX * ky, arcOffsetY / ky);
    }
  }
}
)");
  // clang-format on

  return {cornerSignFunc, ellipseSignFunc};
}

}  // namespace tgfx
