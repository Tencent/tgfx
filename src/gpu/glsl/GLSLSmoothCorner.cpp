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

std::string EmitSmoothCornerFunctions(ShaderBuilder* builder, std::string* ellipticalEntry,
                                      bool stroke, bool localStroke) {
  const auto newtonStepName = builder->getMangledFunctionName("smoothCornerNewtonStep");
  const auto bezierDistName = builder->getMangledFunctionName("smoothCornerBezierDist");
  const auto cornerDistName = builder->getMangledFunctionName("smoothCornerDist");
  // In the localStroke variant the entry functions evaluate distances in the unscaled local
  // frame and report the contour normal at the nearest point (unnormalized), so the caller can
  // convert local distances to device pixels: device distance = local distance /
  // |invTranspose * normal|.
  const auto normalOutParam = localStroke ? ", out vec2 normal" : "";
  const auto normalOutArg = localStroke ? ", normal" : "";
  const auto bezierNormalLine = localStroke ? "  normal = vec2(bestDeriv.y, -bestDeriv.x);\n" : "";

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
  // gives a negative cross product of the curve derivative and the point offset. Strokes iterate
  // three times per seed, fills two.
  const auto iterations = stroke ? 3 : 2;
  builder->addFunction(("highp float " + bezierDistName + R"((highp vec2 p, highp vec2 b0,
    highp vec2 b1, highp vec2 b2, highp vec2 b3)" +
                        normalOutParam + R"() {
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
    for (int j = 0; j < )" +
                        std::to_string(iterations) + R"(; j++) {
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
)" + bezierNormalLine + R"(  return sqrt(best) * (side > 0.0 ? 1.0 : -1.0);
})"));

  // In the localStroke variant the S4 branch sets the normal toward the nearer join point.
  const auto s4Branch = localStroke ? R"(    {
    highp vec2 dps = q - ps;
    highp vec2 dpe = q - pe;
    if (dot(dps, dps) < dot(dpe, dpe)) {
      normal = dps;
      return length(dps);
    }
    normal = dpe;
    return length(dpe);
  }
)"
                                    : R"(    return min(length(q - ps), length(q - pe));
)";
  const auto arcNormalLine = localStroke ? "    normal = arcDir;\n" : "";

  // Signed distance to the smoothed circular corner, negative inside. Segment selection uses the
  // true join-point normals: C1 where fs > 0 and fe > 0, the retained arc where fs < 0 and
  // fe > 0, C3 where both are negative. The remaining combination (fs > 0, fe < 0) lies outside
  // the corner beyond both normals, where the contour is at least the safe distance away, so the
  // distance to the nearer join point is returned; it never underestimates the true distance.
  builder->addFunction(("highp float " + cornerDistName + R"((highp vec2 q, highp float radius,
    highp vec4 coeffs, highp vec3 arcParams)" +
                        normalOutParam + R"() {
  highp float footprint = coeffs.x * radius;
  highp float step = coeffs.y * radius;
  highp float ctrl2 = footprint - 3.0 * step;
  highp vec2 ps = vec2(ctrl2 - coeffs.z * radius, coeffs.w * radius);
  highp vec2 pe = ps.yx;
  highp float fs = arcParams.y * (q.x - ps.x) - arcParams.x * (q.y - ps.y);
  highp float fe = arcParams.x * (q.x - pe.x) - arcParams.y * (q.y - pe.y);
  if (fs > 0.0) {
    if (fe > 0.0) {
      return )" + bezierDistName +
                        R"((q, vec2(footprint, 0.0),
        vec2(footprint - 2.0 * step, 0.0), vec2(ctrl2, 0.0), ps)" +
                        normalOutArg + R"();
    }
)" + s4Branch + R"(  }
  if (fe > 0.0) {
    // The join-point normals pass through the arc center, so the radial projection of a fragment
    // in this sector always lands within the retained sweep and the exact circle distance applies.
    highp vec2 arcDir = q - vec2(radius);
)" + arcNormalLine + R"(    return length(arcDir) - radius;
  }
  return )" + bezierDistName +
                        R"((q, pe, vec2(0.0, ctrl2),
      vec2(0.0, footprint - 2.0 * step), vec2(0.0, footprint))" +
                        normalOutArg + R"();
})"));

  if (ellipticalEntry != nullptr) {
    const auto ellipticalName = builder->getMangledFunctionName("smoothCornerDistElliptical");
    // Elliptical corner: the same true-normal selection with the radii weighting restored, and
    // the retained arc evaluated as a single cubic, the ky-scaled circular-arc approximation.
    builder->addFunction(("highp float " + ellipticalName + R"((highp vec2 q, highp vec2 radii,
      highp vec4 coeffs, highp vec3 arcParams)" +
                          normalOutParam + R"() {
  highp float rx = radii.x;
  highp float ry = radii.y;
  highp float ky = ry / rx;
  highp float footprint = coeffs.x * rx;
  highp float step = coeffs.y * rx;
  highp float ctrl2 = footprint - 3.0 * step;
  highp vec2 ps = vec2(ctrl2 - coeffs.z * rx, coeffs.w * ry);
  highp vec2 pe = vec2(ps.y / ky, ps.x * ky);
  highp float fs = rx * arcParams.y * (q.x - ps.x) - ry * arcParams.x * (q.y - ps.y);
  highp float fe = rx * arcParams.x * (q.x - pe.x) - ry * arcParams.y * (q.y - pe.y);
  if (fs > 0.0) {
    if (fe > 0.0) {
      return )" + bezierDistName +
                          R"((q, vec2(footprint, 0.0),
          vec2(footprint - 2.0 * step, 0.0), vec2(ctrl2, 0.0), ps)" +
                          normalOutArg + R"();
    }
)" + s4Branch + R"(  }
  if (fe > 0.0) {
    // The retained arc as C2b: control handles of arcHandle * rx along the endpoint tangents of
    // the ky-scaled circular construction, matching the CPU path contour exactly.
    highp float handle = arcParams.z * rx;
    return )" + bezierDistName +
                          R"((q, ps, ps + handle * vec2(-arcParams.y, ky * arcParams.x),
          pe - handle * vec2(-arcParams.x, ky * arcParams.y), pe)" +
                          normalOutArg + R"();
  }
  return )" + bezierDistName +
                          R"((q, pe, vec2(0.0, ctrl2 * ky),
          vec2(0.0, (footprint - 2.0 * step) * ky), vec2(0.0, footprint * ky))" +
                          normalOutArg + R"();
})"));
    *ellipticalEntry = ellipticalName;
  }

  return cornerDistName;
}

}  // namespace tgfx
