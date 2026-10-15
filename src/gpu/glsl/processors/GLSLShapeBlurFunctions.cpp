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
#include <algorithm>
#include <string>

namespace tgfx {

// Quadrature point counts the rounded-rect coverage form uses along the axis it does not solve
// in closed form. At a fixed count the quadrature error grows with the ratio between the corner's
// two semi-axes, so the count is picked per shape rather than fixed.
static constexpr int LOW_QUADRATURE_COUNT = 4;
static constexpr int HIGH_QUADRATURE_COUNT = 8;

// The corner semi-axis ratio above which the high count is used instead of the low one.
static constexpr float SEMI_AXIS_RATIO_THRESHOLD = 2.0f;

// Emits the kernel helpers shared by the rectangle and rounded-rect forms.
static inline void AppendKernelFunctions(FragmentShaderBuilder* fragBuilder) {
  // Approximates erf with the rational form from Hastings (1955), published as Abramowitz & Stegun
  // formula 7.1.27 (p. 299).
  //   x       the argument
  //   return  erf(x), in (-1, 1)
  //
  // Valid for 0 <= x < inf with a stated error bound of 5e-4. The sign is applied afterwards since
  // erf is odd.
  fragBuilder->addFunction(R"(
highp float shapeBlurErf(highp float x) {
  highp float ax = abs(x);
  highp float d = 1.0 + ax * (0.278393 + ax * (0.230389 + ax * (0.000972 + ax * 0.078108)));
  highp float d2 = d * d;
  highp float positiveBranch = 1.0 - 1.0 / (d2 * d2);
  return x >= 0.0 ? positiveBranch : -positiveBranch;
}
)");

  // Computes the CDF of the Gaussian truncated to +/- 2 and renormalized over that window.
  //   u       the sigma-normalized coordinate
  //   return  the fraction of the kernel mass below u, in [0, 1]
  //
  // Writing Phi for the untruncated CDF, Phi(u) = 0.5 * (1 + erf(u / sqrt(2))), the result is
  // (Phi(u) - Phi(-2)) / (Phi(2) - Phi(-2)). Sigma is fixed at 1, so the two endpoint terms are
  // constants, written as literals in the code: 0.02275013 is Phi(-2), the tail mass cut off at
  // the left end, and 0.95449974 is Phi(2) - Phi(-2), the mass kept inside the window;
  // 0.70710678 is 1 / sqrt(2).
  fragBuilder->addFunction(R"(
highp float shapeBlurCDF(highp float u) {
  if (u <= -2.0) {
    return 0.0;
  }
  if (u >= 2.0) {
    return 1.0;
  }
  return (0.5 * (1.0 + shapeBlurErf(u * 0.70710678)) - 0.02275013) / 0.95449974;
}
)");
}

void AppendRectBlurFunctions(FragmentShaderBuilder* fragBuilder) {
  AppendKernelFunctions(fragBuilder);

  // Computes the closed-form coverage of a rectangle.
  //   coord     the sigma-normalized point, relative to the rectangle center
  //   halfSize  the sigma-normalized half size on x and y
  //   return    the blurred coverage at that point, in [0, 1]
  //
  // The blurred coverage of a rectangle spanning [x0, x1] x [y0, y1] is the product of one CDF
  // difference per axis:
  //   C(p) = (CDF(px - x0) - CDF(px - x1)) * (CDF(py - y0) - CDF(py - y1))
  // The rectangle is centered at the origin, so x0 = -halfSize.x, x1 = +halfSize.x: the arguments
  // below are px - x0 = lo.x and px - x1 = hi.x, and likewise for y.
  fragBuilder->addFunction(R"(
highp float shapeBlurRectCoverage(highp vec2 coord, highp vec2 halfSize) {
  highp vec2 lo = coord + halfSize;
  highp vec2 hi = coord - halfSize;
  return (shapeBlurCDF(lo.x) - shapeBlurCDF(hi.x)) * (shapeBlurCDF(lo.y) - shapeBlurCDF(hi.y));
}
)");
}

int GetRRectBlurQuadratureCount(const Point& cornerOverSigma) {
  // A zero corner (no arc) makes every slice identical along the summed axis, so the
  // quadrature's sampling error is a common factor of the sum and the weight sum, which cancels
  // in the division — any count is exact, and the low count suffices.
  if (cornerOverSigma.x <= 0.0f || cornerOverSigma.y <= 0.0f) {
    return LOW_QUADRATURE_COUNT;
  }
  // Worst errors measured over 337 sigma-normalized shapes (corner semi-axes from 0.25 to 10,
  // six half-size pairs), in 8-bit levels: 16.01 with the low count everywhere, 7.90 with the
  // high count everywhere. With this dispatch, shapes below the threshold reach only 6.16 and
  // the overall worst stays 7.90.
  //
  // The threshold stays at 2: at 2.5 the low-count worst would rise from 6.16 to 7.57.
  const auto ratio =
      std::max(cornerOverSigma.x / cornerOverSigma.y, cornerOverSigma.y / cornerOverSigma.x);
  return ratio > SEMI_AXIS_RATIO_THRESHOLD ? HIGH_QUADRATURE_COUNT : LOW_QUADRATURE_COUNT;
}

void AppendRRectBlurFunctions(FragmentShaderBuilder* fragBuilder, int quadratureCount) {
  AppendKernelFunctions(fragBuilder);

  // Computes the coverage at a point convolved along one axis alone, bounded by the shape's half
  // extent at that point's cross coordinate. "Along" names the axis being convolved and "cross" the
  // perpendicular one; the caller decides which shape axis plays each role.
  //   alongCoord   the sigma-normalized coordinate on the convolved axis
  //   crossCoord   the sigma-normalized coordinate on the perpendicular axis
  //   cornerAlong  the sigma-normalized corner semi-axis on the convolved axis
  //   cornerCross  the sigma-normalized corner semi-axis on the perpendicular axis
  //   halfAlong    the sigma-normalized half size on the convolved axis
  //   halfCross    the sigma-normalized half size on the perpendicular axis
  //   return       the coverage at that point, in [0, 1]
  //
  // The shape's half width at cross coordinate c is, with d = |c| - (hc - rc):
  //   straight region (d <= 0):  w = ha
  //   corner region (d > 0):     w = ha - ra + (ra / rc) * sqrt(rc^2 - d^2)
  // and the coverage is CDF(alongCoord + w) - CDF(alongCoord - w). The short names stand for
  // the parameters: c = crossCoord, ha = halfAlong, hc = halfCross, ra = cornerAlong,
  // rc = cornerCross. In the code, curved is w and delta is min(-d, 0), so the straight region
  // needs no branch: delta = 0 makes the sqrt term rc and w collapses to ha.
  //
  // Callers guarantee cornerAlong <= halfAlong and cornerCross <= halfCross.
  fragBuilder->addFunction(R"(
highp float shapeBlurSliceCoverage(highp float alongCoord, highp float crossCoord,
                             highp float cornerAlong, highp float cornerCross,
                             highp float halfAlong, highp float halfCross) {
  highp float delta = min(halfCross - cornerCross - abs(crossCoord), 0.0);
  highp float ratio = cornerAlong / max(cornerCross, 1e-6);
  highp float curved = halfAlong - cornerAlong +
                       ratio * sqrt(max(0.0, cornerCross * cornerCross - delta * delta));
  return shapeBlurCDF(alongCoord + curved) - shapeBlurCDF(alongCoord - curved);
}
)");

  // Computes the coverage of a rounded rectangle.
  //   coord     the sigma-normalized point, relative to the shape center
  //   halfSize  the sigma-normalized half size on x and y
  //   corner    the sigma-normalized corner semi-axes on x and y
  //   return    the blurred coverage at that point, in [0, 1]
  //
  // The coverage is a nested integral over the kernel g, with w(y) the shape's half width on
  // the slice at y:
  //   C(p) = int [ int_{-w(y)}^{+w(y)} g(px - x) dx ] * g(py - y) dy
  // The inner integral is closed form (shapeBlurSliceCoverage). The outer one is a fixed
  // midpoint sum over N quadrature points s_i = start + (i + 0.5) * step, where slice_i (the
  // code's span) is the inner integral on the slice offset s_i from p, and
  // weight_i = exp(-0.5 * s_i^2); the sum is rescaled by the exact window mass:
  //   C(p) = [sum_i weight_i * slice_i] * (CDF(end) - CDF(start)) / sum_i weight_i
  //
  // The quadrature axis follows the smaller coordinate component so the outer interval is more
  // likely to span the full kernel window, which lowers the quadrature error. Swapping the axes
  // only reorders the two integrals, so the choice affects that error and not the value being
  // approximated.
  //
  // Coverage is always 1 inside the shape inset by the kernel support of 2, so the quadrature can
  // be skipped there. Subtracting the corner keeps this box test inside that inset shape.
  auto quadrature = std::string(R"(
highp float shapeBlurRRectCoverage(highp vec2 coord, highp vec2 halfSize, highp vec2 corner) {
  if (all(lessThan(abs(coord), halfSize - (corner + vec2(2.0))))) {
    return 1.0;
  }

  const int N = )") +
                    std::to_string(quadratureCount) + R"(;
  highp float lo;
  highp float hi;
  bool swapAxes;
  if (abs(coord.x) > abs(coord.y)) {
    lo = coord.y - halfSize.y;
    hi = coord.y + halfSize.y;
    swapAxes = false;
  } else {
    lo = coord.x - halfSize.x;
    hi = coord.x + halfSize.x;
    swapAxes = true;
  }
  highp float start = min(max(-2.0, lo), hi);
  highp float end = min(max(2.0, lo), hi);
  if (start == end) {
    return 0.0;
  }
  highp float step = (end - start) / float(N);
  highp float s = start + step * 0.5;
  highp float accum = 0.0;
  highp float weightSum = 0.0;
  for (int i = 0; i < N; i++) {
    highp float weight = exp(-0.5 * s * s);
    highp float span;
    if (swapAxes) {
      span = shapeBlurSliceCoverage(coord.y, coord.x - s, corner.y, corner.x, halfSize.y,
                                    halfSize.x);
    } else {
      span = shapeBlurSliceCoverage(coord.x, coord.y - s, corner.x, corner.y, halfSize.x,
                                    halfSize.y);
    }
    accum += span * weight;
    weightSum += weight;
    s += step;
  }
  return accum * (shapeBlurCDF(end) - shapeBlurCDF(start)) / weightSum;
}
)";
  fragBuilder->addFunction(quadrature);
}

void AppendRectMaskFunctions(FragmentShaderBuilder* fragBuilder) {
  // Returns the signed distance to a rectangle.
  //   p         the point in content pixels, relative to the rectangle center
  //   halfSize  the half size on x and y, in content pixels
  //   return    the distance in content pixels, positive outside and negative inside
  //
  // The mask helpers work in content pixels rather than the sigma-normalized space the blur forms
  // above use.
  fragBuilder->addFunction(R"(
highp float shapeMaskRectSDF(highp vec2 p, highp vec2 halfSize) {
  highp vec2 d = abs(p) - halfSize;
  return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
}
)");

  // Converts a signed distance to coverage over a one pixel wide linear ramp.
  //   sdf     the signed distance in content pixels, positive outside the shape
  //   return  the coverage at that distance, in [0, 1]
  fragBuilder->addFunction(R"(
highp float shapeMaskCoverage(highp float sdf) {
  return clamp(0.5 - sdf, 0.0, 1.0);
}
)");
}

void AppendRRectMaskFunctions(FragmentShaderBuilder* fragBuilder) {
  AppendRectMaskFunctions(fragBuilder);

  // Returns the signed distance to a rounded rectangle.
  //   p         the point in content pixels, relative to the shape center
  //   halfSize  the half size on x and y, in content pixels
  //   corner    the corner semi-axes on x and y, in content pixels
  //   return    the distance in content pixels, positive outside and negative inside
  //
  // Outside the corner regions the bounding rectangle gives the exact distance. Inside one, d is
  // the offset from the corner ellipse center and r its semi-axes, so F = dot(d / r, d / r) - 1
  // vanishes on the ellipse; near the boundary F grows as length(grad F) times the distance, so
  // F / length(grad F) approximates it. An exact distance to an ellipse has no closed form.
  //
  // The radii are clamped so a radius larger than the shape degenerates to a stadium rather than
  // inverting the inner rectangle, and kept away from zero so the division stays finite.
  fragBuilder->addFunction(R"(
highp float shapeMaskRRectSDF(highp vec2 p, highp vec2 halfSize, highp vec2 corner) {
  highp vec2 r = max(min(corner, halfSize), vec2(1e-6));
  highp vec2 inner = halfSize - r;
  highp vec2 d = abs(p) - inner;
  if (d.x <= 0.0 || d.y <= 0.0) {
    return shapeMaskRectSDF(p, halfSize);
  }
  highp vec2 offset = d / r;
  highp float test = dot(offset, offset) - 1.0;
  highp vec2 grad = 2.0 * offset / r;
  return test / max(length(grad), 1e-20);
}
)");
}

}  // namespace tgfx
