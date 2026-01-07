/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "core/utils/PathUtils.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/PointUtils.h"
#include "tgfx/core/Point.h"

namespace tgfx {

namespace {

int ValidUnitDivide(float numer, float denom, float* ratio) {
  DEBUG_ASSERT(ratio);
  if (numer < 0) {
    numer = -numer;
    denom = -denom;
  }

  if (denom == 0 || numer == 0 || numer >= denom) {
    return 0;
  }

  float r = numer / denom;
  if (std::isnan(r)) {
    return 0;
  }
  DEBUG_ASSERT((r >= 0 && r < 1.f));
  if (r == 0) {
    return 0;
  }
  *ratio = r;
  return 1;
}

int FindUnitQuadRoots(float A, float B, float C, float roots[2]) {
  DEBUG_ASSERT(roots);

  if (A == 0) {
    return ValidUnitDivide(-C, B, roots);
  }

  auto* r = roots;
  // Use double precision to prevent overflow
  double doubleRadius = static_cast<double>(B * B) - (4.0 * static_cast<double>(A * C));
  if (doubleRadius < 0) {
    return 0;
  }
  doubleRadius = std::sqrt(doubleRadius);
  auto radius = static_cast<float>(doubleRadius);
  if (!FloatsAreFinite(&radius, 1)) {
    return 0;
  }

  float Q = (B < 0) ? -(B - radius) / 2 : -(B + radius) / 2;
  r += ValidUnitDivide(Q, A, r);
  r += ValidUnitDivide(C, Q, r);
  if (r - roots == 2) {
    if (roots[0] > roots[1]) {
      using std::swap;
      swap(roots[0], roots[1]);
    } else if (roots[0] == roots[1]) {
      r -= 1;
    }
  }
  return static_cast<int>(r - roots);
}

int FindCubicInflections(const Point src[4], float tValues[2]) {
  float Ax = src[1].x - src[0].x;
  float Ay = src[1].y - src[0].y;
  float Bx = src[2].x - (2 * src[1].x) + src[0].x;
  float By = src[2].y - (2 * src[1].y) + src[0].y;
  float Cx = src[3].x + (3 * (src[1].x - src[2].x)) - src[0].x;
  float Cy = src[3].y + (3 * (src[1].y - src[2].y)) - src[0].y;

  return FindUnitQuadRoots((Bx * Cy) - (By * Cx), (Ax * Cy) - (Ay * Cx), (Ax * By) - (Ay * Bx),
                           tValues);
}

void ChopCubicAt(const Point source[4], Point destination[7], float t) {
  DEBUG_ASSERT((0 <= t && t <= 1));

  if (t == 1) {
    memcpy(destination, source, sizeof(Point) * 4);
    destination[4] = destination[5] = destination[6] = source[3];
    return;
  }

  auto p0 = source[0];
  auto p1 = source[1];
  auto p2 = source[2];
  auto p3 = source[3];
  auto T = Point::Make(t, t);

  auto unchecked_mix = [](const Point& a, const Point& b, const Point& t) -> Point {
    Point ret;
    ret.x = (b.x - a.x) * t.x + a.x;
    ret.y = (b.y - a.y) * t.y + a.y;
    return ret;
  };

  auto ab = unchecked_mix(p0, p1, T);
  auto bc = unchecked_mix(p1, p2, T);
  auto cd = unchecked_mix(p2, p3, T);
  auto abc = unchecked_mix(ab, bc, T);
  auto bcd = unchecked_mix(bc, cd, T);
  auto abcd = unchecked_mix(abc, bcd, T);

  destination[0] = p0;
  destination[1] = ab;
  destination[2] = abc;
  destination[3] = abcd;
  destination[4] = bcd;
  destination[5] = cd;
  destination[6] = p3;
}

void ChopCubicAt(const Point source[4], Point destination[10], float t0, float t1) {
  DEBUG_ASSERT((0 <= t0 && t0 <= t1 && t1 <= 1));

  if (t1 == 1) {
    ChopCubicAt(source, destination, t0);
    destination[7] = destination[8] = destination[9] = source[3];
    return;
  }

  // Perform both chops in parallel using 4-lane SIMD.
  auto p00 = std::make_pair(source[0], source[0]);
  auto p11 = std::make_pair(source[1], source[1]);
  auto p22 = std::make_pair(source[2], source[2]);
  auto p33 = std::make_pair(source[3], source[3]);
  std::pair<Point, Point> T = {Point::Make(t0, t0), Point::Make(t1, t1)};

  auto unchecked_mix = [](const std::pair<Point, Point>& a, const std::pair<Point, Point>& b,
                          const std::pair<Point, Point>& t) -> std::pair<Point, Point> {
    std::pair<Point, Point> ret;
    ret.first.x = (b.first.x - a.first.x) * t.first.x + a.first.x;
    ret.first.y = (b.first.y - a.first.y) * t.first.y + a.first.y;
    ret.second.x = (b.second.x - a.second.x) * t.second.x + a.second.x;
    ret.second.y = (b.second.y - a.second.y) * t.second.y + a.second.y;
    return ret;
  };

  auto ab = unchecked_mix(p00, p11, T);
  auto bc = unchecked_mix(p11, p22, T);
  auto cd = unchecked_mix(p22, p33, T);
  auto abc = unchecked_mix(ab, bc, T);
  auto bcd = unchecked_mix(bc, cd, T);
  auto abcd = unchecked_mix(abc, bcd, T);
  auto middle = unchecked_mix(abc, bcd, {T.second, T.first});

  destination[0] = p00.first;
  destination[1] = ab.first;
  destination[2] = abc.first;
  destination[3] = abcd.first;
  destination[4] = middle.first;
  destination[5] = middle.second;
  destination[6] = abcd.second;
  destination[7] = bcd.second;
  destination[8] = cd.second;
  destination[9] = p33.second;
}

void ChopCubicAt(const Point source[4], Point destination[], const float toleranceValues[],
                 int toleranceCount) {
  DEBUG_ASSERT(std::all_of(toleranceValues, toleranceValues + toleranceCount,
                           [](float t) { return t >= 0 && t <= 1; }));
  DEBUG_ASSERT(std::is_sorted(toleranceValues, toleranceValues + toleranceCount));

  if (destination) {
    if (toleranceCount == 0) {  // nothing to chop
      memcpy(destination, source, 4 * sizeof(Point));
    } else {
      int i = 0;
      for (; i < toleranceCount - 1; i += 2) {
        // Do two chops at once.
        float tolerance1 = *(toleranceValues + i);
        float tolerance2 = *(toleranceValues + i + 1);
        if (i != 0) {
          float lastTolerance = toleranceValues[i - 1];
          tolerance1 = std::clamp((tolerance1 - lastTolerance) / (1 - lastTolerance), 0.f, 1.f);
          tolerance2 = std::clamp((tolerance2 - lastTolerance) / (1 - lastTolerance), 0.f, 1.f);
        }
        ChopCubicAt(source, destination, tolerance1, tolerance2);
        source = destination = destination + 6;
      }
      if (i < toleranceCount) {
        // Chop the final cubic if there was an odd number of chops.
        DEBUG_ASSERT(i + 1 == toleranceCount);
        float tolerance = toleranceValues[i];
        if (i != 0) {
          float lastTolerance = toleranceValues[i - 1];
          tolerance = std::clamp((tolerance - lastTolerance) / (1 - lastTolerance), 0.f, 1.f);
        }
        ChopCubicAt(source, destination, tolerance);
      }
    }
  }
}

void ChopCubicAtHalf(const Point src[4], Point dst[7]) {
  ChopCubicAt(src, dst, 0.5f);
}

int ChopCubicAtInflections(const Point source[4], Point destination[10]) {
  float toleranceValues[2];
  int count = FindCubicInflections(source, toleranceValues);

  if (destination) {
    if (count == 0) {
      memcpy(destination, source, 4 * sizeof(Point));
    } else {
      ChopCubicAt(source, destination, toleranceValues, count);
    }
  }
  return count + 1;
}

void ConvertNoninflectCubicToQuads(const Point p[4], float toleranceSqd, std::vector<Point>& quads,
                                   int sublevel = 0, bool preserveFirstTangent = true,
                                   bool preserveLastTangent = true) {
  // Notation: Point a is always p[0]. Point b is p[1] unless p[1] == p[0], in which case it is
  // p[2]. Point d is always p[3]. Point c is p[2] unless p[2] == p[3], in which case it is p[1].
  auto ab = p[1] - p[0];
  auto dc = p[2] - p[3];

  if (Point::DotProduct(ab, ab) < FLOAT_NEARLY_ZERO) {
    if (Point::DotProduct(dc, dc) < FLOAT_NEARLY_ZERO) {
      quads.push_back(p[0]);
      quads.push_back(p[0]);
      quads.push_back(p[3]);
      return;
    }
    ab = p[2] - p[0];
  }
  if (Point::DotProduct(dc, dc) < FLOAT_NEARLY_ZERO) {
    dc = p[1] - p[3];
  }

  constexpr float lengthScale = 3.f * 1.f / 2.f;
  constexpr int maxSubdivs = 10;

  ab *= lengthScale;
  dc *= lengthScale;

  // c0 and c1 are extrapolations along vectors ab and dc.
  auto c0 = p[0] + ab;
  auto c1 = p[3] + dc;

  // When recursion depth exceeds the limit, force distanceSqd to 0 to accept the approximation.
  // This prevents infinite recursion while maintaining acceptable visual quality, as curves
  // requiring more than maxSubdivs levels are already extremely well-approximated.
  float distanceSqd = sublevel > maxSubdivs ? 0 : PointUtils::DistanceSquared(c0, c1);
  if (distanceSqd < toleranceSqd) {
    Point newC;
    if (preserveFirstTangent == preserveLastTangent) {
      // We used to force a split when both tangents need to be preserved and c0 != c1.
      // This introduced a large performance regression for tiny paths for no noticeable
      // quality improvement. However, we aren't quite fulfilling our contract of guaranteeing
      // the two tangent vectors and this could introduce a missed pixel in
      // AAHairlinePathRenderer.
      newC = (c0 + c1) * 0.5f;
    } else if (preserveFirstTangent) {
      newC = c0;
    } else {
      newC = c1;
    }

    quads.push_back(p[0]);
    quads.push_back(newC);
    quads.push_back(p[3]);
    return;
  }
  Point choppedPoints[7];
  ChopCubicAtHalf(p, choppedPoints);
  ConvertNoninflectCubicToQuads(choppedPoints + 0, toleranceSqd, quads, sublevel + 1,
                                preserveFirstTangent, false);
  ConvertNoninflectCubicToQuads(choppedPoints + 3, toleranceSqd, quads, sublevel + 1, false,
                                preserveLastTangent);
}

inline Point interp(const Point& v0, const Point& v1, float t) {
  return v0 + (v1 - v0) * t;
}
}  // namespace

std::vector<Point> PathUtils::ConvertCubicToQuads(const Point cubicPoints[4], float tolerance) {
  if (!FloatsAreFinite(&tolerance, 1)) {
    return {};
  }
  Point chopped[10];
  int count = ChopCubicAtInflections(cubicPoints, chopped);

  float toleranceSquare = tolerance * tolerance;

  std::vector<Point> convertQuads;
  for (int i = 0; i < count; ++i) {
    Point* cubic = chopped + (3 * i);
    ConvertNoninflectCubicToQuads(cubic, toleranceSquare, convertQuads);
  }
  return convertQuads;
}

void PathUtils::ChopQuadAt(const Point src[3], Point dst[5], float t) {
  DEBUG_ASSERT((t > 0.f && t < 1.f));

  auto p0 = src[0];
  auto p1 = src[1];
  auto p2 = src[2];

  auto p01 = interp(p0, p1, t);
  auto p12 = interp(p1, p2, t);

  dst[0] = p0;
  dst[1] = p01;
  dst[2] = interp(p01, p12, t);
  dst[3] = p12;
  dst[4] = p2;
}

float PathUtils::FindQuadMaxCurvature(const Point src[3]) {
  float Ax = src[1].x - src[0].x;
  float Ay = src[1].y - src[0].y;
  float Bx = src[0].x - src[1].x - src[1].x + src[2].x;
  float By = src[0].y - src[1].y - src[1].y + src[2].y;

  float numer = -((Ax * Bx) + (Ay * By));
  float denom = (Bx * Bx) + (By * By);
  if (denom < 0) {
    numer = -numer;
    denom = -denom;
  }
  if (numer <= 0) {
    return 0;
  }
  if (numer >= denom) {  // Also catches denom=0.
    return 1;
  }
  float t = numer / denom;
  DEBUG_ASSERT(((0 <= t && t < 1) || std::isnan(t)));
  return t;
}

int PathUtils::ChopQuadAtMaxCurvature(const Point src[3], Point dst[5]) {
  float t = FindQuadMaxCurvature(src);
  if (t > 0 && t < 1) {
    ChopQuadAt(src, dst, t);
    return 2;
  } else {
    memcpy(dst, src, 3 * sizeof(Point));
    return 1;
  }
}

QuadUVMatrix::QuadUVMatrix(const Point controlPoints[3]) {
  this->set(controlPoints);
}

void QuadUVMatrix::set(const Point controlPoints[3]) {
  // We want M such that M * xy_pt = uv_pt
  // We know M * control_pts = [0  1/2 1]
  //                           [0  0   1]
  //                           [1  1   1]
  // And control_pts = [x0 x1 x2]
  //                   [y0 y1 y2]
  //                   [1  1  1 ]
  // We invert the control pt matrix and post concat to both sides to get M.
  // Using the known form of the control point matrix and the result, we can
  // optimize and improve precision.

  double x0 = controlPoints[0].x;
  double y0 = controlPoints[0].y;
  double x1 = controlPoints[1].x;
  double y1 = controlPoints[1].y;
  double x2 = controlPoints[2].x;
  double y2 = controlPoints[2].y;

  // pre-calculate some adjugate matrix factors for determinant
  double a2 = (x1 * y2) - (x2 * y1);
  double a5 = (x2 * y0) - (x0 * y2);
  double a8 = (x0 * y1) - (x1 * y0);
  double det = a2 + a5 + a8;

  if (!std::isfinite(det) ||
      FloatNearlyZero(static_cast<float>(det), FLOAT_NEARLY_ZERO * FLOAT_NEARLY_ZERO)) {
    // The quad is degenerate. Hopefully this is rare. Find the pts that are
    // farthest apart to compute a line (unless it is really a pt).
    auto maxD = PointUtils::DistanceSquared(controlPoints[0], controlPoints[1]);
    int maxEdge = 0;
    auto d = PointUtils::DistanceSquared(controlPoints[1], controlPoints[2]);
    if (d > maxD) {
      maxD = d;
      maxEdge = 1;
    }
    d = PointUtils::DistanceSquared(controlPoints[2], controlPoints[0]);
    if (d > maxD) {
      maxD = d;
      maxEdge = 2;
    }
    // We could have a tolerance here, not sure if it would improve anything
    if (maxD > 0) {
      // Set the matrix to give (u = 0, v = distance_to_line)
      auto lineVec = controlPoints[(maxEdge + 1) % 3] - controlPoints[maxEdge];
      // when looking from the point 0 down the line we want positive
      // distances to be to the left. This matches the non-degenerate
      // case.
      lineVec = PointUtils::MakeOrthogonal(lineVec, PointUtils::Side::Left);
      // first row
      matrix[0] = 0;
      matrix[1] = 0;
      matrix[2] = 0;
      // second row
      matrix[3] = lineVec.x;
      matrix[4] = lineVec.y;
      matrix[5] = -Point::DotProduct(lineVec, controlPoints[maxEdge]);
    } else {
      // It's a point. It should cover zero area. Just set the matrix such
      // that (u, v) will always be far away from the quad.
      matrix[0] = 0;
      matrix[1] = 0;
      matrix[2] = 100.f;
      matrix[3] = 0;
      matrix[4] = 0;
      matrix[5] = 100.f;
    }
  } else {
    double scale = 1.0 / det;

    // compute adjugate matrix
    double a3;
    double a4;
    double a6;
    double a7;
    a3 = y2 - y0;
    a4 = x0 - x2;

    a6 = y0 - y1;
    a7 = x1 - x0;

    // this performs the uv_pts*adjugate(control_pts) multiply,
    // then does the scale by 1/det afterwards to improve precision
    matrix[0] = static_cast<float>((0.5 * a3 + a6) * scale);
    matrix[1] = static_cast<float>((0.5 * a4 + a7) * scale);
    matrix[2] = static_cast<float>((0.5 * a5 + a8) * scale);
    matrix[3] = static_cast<float>(a6 * scale);
    matrix[4] = static_cast<float>(a7 * scale);
    matrix[5] = static_cast<float>(a8 * scale);
  }
}

void QuadUVMatrix::apply(void* vertices, int vertexCount, size_t stride, size_t uvOffset) const {
  auto* xyPtr = static_cast<char*>(vertices);
  auto* uvPtr = static_cast<char*>(vertices) + uvOffset;
  float sx = matrix[0];
  float kx = matrix[1];
  float tx = matrix[2];
  float ky = matrix[3];
  float sy = matrix[4];
  float ty = matrix[5];
  for (int i = 0; i < vertexCount; ++i) {
    auto* xy = reinterpret_cast<const Point*>(xyPtr);
    auto* uv = reinterpret_cast<Point*>(uvPtr);
    uv->x = sx * xy->x + kx * xy->y + tx;
    uv->y = ky * xy->x + sy * xy->y + ty;
    xyPtr += stride;
    uvPtr += stride;
  }
}

}  // namespace tgfx