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

#include "GLSLShapeBlurFunctions.h"
#include <string>

namespace tgfx {

void AppendShapeBlurFunctions(FragmentShaderBuilder* fragBuilder) {
  // erf approximation from Hastings (1955), published as Abramowitz & Stegun formula 7.1.27
  // (p. 299). Valid for 0 <= x < inf with a stated error bound of 5e-4. It is a pure rational
  // form, so no exp() is needed. The sign is applied afterwards since erf is odd.
  fragBuilder->addFunction(R"(
float shapeBlurErf(float x) {
  float ax = abs(x);
  float d = 1.0 + ax * (0.278393 + ax * (0.230389 + ax * (0.000972 + ax * 0.078108)));
  float d2 = d * d;
  float positiveBranch = 1.0 - 1.0 / (d2 * d2);
  return x >= 0.0 ? positiveBranch : -positiveBranch;
}
)");

  // CDF of the Gaussian truncated to +/- 2 and renormalized over that window. Coordinates reaching
  // here are already divided by sigma, so sigma is 1 and both end masses are compile-time
  // constants: LOW is Phi(-2) and MASS is Phi(2) - Phi(-2). 0.70710678 is 1 / sqrt(2).
  fragBuilder->addFunction(R"(
float shapeBlurCDF(float u) {
  if (u <= -2.0) {
    return 0.0;
  }
  if (u >= 2.0) {
    return 1.0;
  }
  return (0.5 * (1.0 + shapeBlurErf(u * 0.70710678)) - 0.02275013) / 0.95449974;
}
)");

  // Closed-form coverage of an axis-aligned rectangle: the indicator is separable, so the result is
  // the product of one CDF difference per axis. No sampling.
  fragBuilder->addFunction(R"(
float shapeBlurRectCoverage(vec2 coord, vec2 halfSize) {
  vec2 lo = coord + halfSize;
  vec2 hi = coord - halfSize;
  return (shapeBlurCDF(lo.x) - shapeBlurCDF(hi.x)) * (shapeBlurCDF(lo.y) - shapeBlurCDF(hi.y));
}
)");
}

void AppendRoundRectBlurFunctions(FragmentShaderBuilder* fragBuilder) {
  AppendShapeBlurFunctions(fragBuilder);

  // Horizontal blurred coverage of a single row (the inner, closed-form integral). cornerAlong is
  // the corner semi-axis along the integration axis and decides where the straight edge ends;
  // cornerCross is the semi-axis across it and decides how far the row narrows. Sigma normalization
  // turns an originally circular corner into an elliptical one, hence the two semi-axes; they are
  // equal in the isotropic case and the formula degenerates to a circular arc.
  //
  // Callers guarantee cornerAlong <= halfAlong and cornerCross <= halfCross.
  fragBuilder->addFunction(R"(
float shapeBlurRowSpan(float x, float y, float cornerAlong, float cornerCross,
                       float halfCross, float halfAlong) {
  float delta = min(halfAlong - cornerAlong - abs(y), 0.0);
  float ratio = cornerCross / max(cornerAlong, 1e-6);
  float curved = halfCross - cornerCross +
                 ratio * sqrt(max(0.0, cornerAlong * cornerAlong - delta * delta));
  return shapeBlurCDF(x + curved) - shapeBlurCDF(x - curved);
}
)");

  // Rounded-rect coverage: the inner integral is closed form per row, the outer one is a fixed
  // midpoint quadrature. The integration axis follows the smaller coordinate component so the
  // interval is more likely to span the full kernel window. Swapping axes is only a Fubini
  // reorder, so the choice affects sampling efficiency and not the result.
  auto quadrature = std::string(R"(
float shapeBlurRoundRectCoverage(vec2 coord, vec2 halfSize, vec2 corner) {
  const int N = )") +
                    std::to_string(ShapeBlurQuadratureCount) + R"(;
  float alongCoord;
  float alongHalf;
  bool swapAxes;
  if (abs(coord.x) > abs(coord.y)) {
    alongCoord = coord.y;
    alongHalf = halfSize.y;
    swapAxes = false;
  } else {
    alongCoord = coord.x;
    alongHalf = halfSize.x;
    swapAxes = true;
  }
  float lo = alongCoord - alongHalf;
  float hi = alongCoord + alongHalf;
  float start = min(max(-2.0, lo), hi);
  float end = min(max(2.0, lo), hi);
  if (start == end) {
    return 0.0;
  }
  float step = (end - start) / float(N);
  float s = start + step * 0.5;
  float accum = 0.0;
  float weightSum = 0.0;
  for (int i = 0; i < N; i++) {
    float weight = exp(-0.5 * s * s);
    float span;
    if (swapAxes) {
      span = shapeBlurRowSpan(coord.y, coord.x - s, corner.x, corner.y, halfSize.y, halfSize.x);
    } else {
      span = shapeBlurRowSpan(coord.x, coord.y - s, corner.y, corner.x, halfSize.x, halfSize.y);
    }
    accum += span * weight;
    weightSum += weight;
    s += step;
  }
  if (weightSum <= 0.0) {
    return 0.0;
  }
  return accum * (shapeBlurCDF(end) - shapeBlurCDF(start)) / weightSum;
}
)";
  fragBuilder->addFunction(quadrature);
}

void AppendShapeMaskFunctions(FragmentShaderBuilder* fragBuilder) {
  // Signed distance to an axis-aligned rectangle, p relative to its center. Positive outside,
  // negative inside.
  fragBuilder->addFunction(R"(
float shapeMaskRectSDF(vec2 p, vec2 halfSize) {
  vec2 d = abs(p) - halfSize;
  return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
}
)");

  // Converts a signed distance to coverage over a one pixel wide linear ramp, matching the
  // antialiasing the rest of the project uses for shapes. The caller guarantees the distance is
  // measured in a space whose scale to device pixels is uniform and close to one.
  fragBuilder->addFunction(R"(
float shapeMaskCoverage(float sdf) {
  return clamp(0.5 - sdf, 0.0, 1.0);
}
)");
}

void AppendRoundRectMaskFunctions(FragmentShaderBuilder* fragBuilder) {
  AppendShapeMaskFunctions(fragBuilder);

  // Signed distance to a rounded rectangle whose corners may be elliptical. Outside the corner
  // boxes the shape is a rectangle, so the rectangle distance is exact. Inside a corner box the
  // implicit ellipse equation is divided by its gradient magnitude, which is the same first-order
  // distance the project's ellipse coverage uses; an exact distance to an ellipse has no closed
  // form. The radii are clamped so a radius larger than the shape degenerates to a stadium rather
  // than inverting the inner rectangle, and kept away from zero so the division stays finite.
  fragBuilder->addFunction(R"(
float shapeMaskRoundRectSDF(vec2 p, vec2 halfSize, vec2 corner) {
  vec2 r = max(min(corner, halfSize), vec2(1e-6));
  vec2 inner = halfSize - r;
  vec2 d = abs(p) - inner;
  if (d.x <= 0.0 || d.y <= 0.0) {
    return shapeMaskRectSDF(p, halfSize);
  }
  vec2 offset = d / r;
  float test = dot(offset, offset) - 1.0;
  vec2 grad = 2.0 * offset / r;
  return test / max(length(grad), 1e-20);
}
)");
}

}  // namespace tgfx
