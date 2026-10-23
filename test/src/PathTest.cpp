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

#include <chrono>
#include <cmath>
#include <cstddef>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#include <include/core/SkPath.h>
#pragma clang diagnostic pop
#include "base/TGFXTest.h"
#include "core/NoConicsPathIterator.h"
#include "core/PathRasterizer.h"
#include "core/PathRef.h"
#include "core/PathTriangulator.h"
#include "core/ShapeRasterizer.h"
#include "core/SmoothCornerCoeffs.h"
#include "gtest/gtest.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Surface.h"
#include "tgfx/svg/SVGPathParser.h"
#include "utils/Baseline.h"
#include "utils/ContextScope.h"

namespace tgfx {

TGFX_TEST(PathTest, AvoidInfiniteLoop) {
  auto path = SVGPathParser::FromSVGString(
      "M3.1 1L5.1999 1L5.1999 3.1L4.6399 3.1L4.6399 1.956L1.956 4.6399L3.1 4.6399L3.1 5.1999L1 "
      "5.1999L1 3.1L1.56 3.1L1.56 4.244L4.244 1.56L3.1 1.56L3.1 1Z");

  {
    // infinite loop path: must return without hanging.
    // Return count may vary across platforms due to float precision.
    ASSERT_FALSE(PathTriangulator::ShouldTriangulatePath(*path));
    auto bounds = path->getBounds();
    std::vector<float> vertices = {};
    PathTriangulator::ToAATriangles(*path, bounds, &vertices);
  }

  {
    // scale up 100, succeed
    path->transform(Matrix::MakeScale(100, 100));
    ASSERT_TRUE(PathTriangulator::ShouldTriangulatePath(*path));
    auto bounds = path->getBounds();
    std::vector<float> vertices = {};
    PathTriangulator::ToAATriangles(*path, bounds, &vertices);
  }

  {
    // scale down to 0.5, succeed
    path->transform(Matrix::MakeScale(0.005f, 0.005f));
    ASSERT_FALSE(PathTriangulator::ShouldTriangulatePath(*path));
    auto bounds = path->getBounds();
    std::vector<float> vertices = {};
    PathTriangulator::ToAATriangles(*path, bounds, &vertices);
  }
}

TGFX_TEST(PathTest, DrawInfiniteLoopPath) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 16, 16);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::Black());

  {
    auto path = SVGPathParser::FromSVGString(
        "M3.1 1L5.1999 1L5.1999 3.1L4.6399 3.1L4.6399 1.956L1.956 4.6399L3.1 4.6399L3.1 5.1999L1 "
        "5.1999L1 3.1L1.56 3.1L1.56 4.244L4.244 1.56L3.1 1.56L3.1 1Z");

    Paint paint;
    paint.setColor(Color::FromRGBA(255, 255, 0, 255));
    canvas->drawPath(*path, paint);
  }

  EXPECT_TRUE(Baseline::Compare(surface, "PathTest/DrawInfiniteLoopPath"));
}

TGFX_TEST(PathTest, ZeroDimensionPathRasterization) {
  {
    //normal case
    Path path;
    path.moveTo(10.f, 10.f);
    path.lineTo(110.f, 110.f);
    auto bound = path.getBounds();
    ASSERT_EQ(bound.width(), 100.f);
    ASSERT_EQ(bound.height(), 100.f);
    ASSERT_FALSE(PathTriangulator::ShouldTriangulatePath(path));
    auto rasterizer = PathRasterizer::MakeFrom(static_cast<int>(bound.width()),
                                               static_cast<int>(bound.height()), path, true);
    ASSERT_TRUE(rasterizer != nullptr);
  }
  {
    // height is zero
    Path path;
    path.moveTo(10.f, 10.f);
    path.lineTo(110.f, 10.f);
    auto bound = path.getBounds();
    ASSERT_EQ(bound.width(), 100.f);
    ASSERT_EQ(bound.height(), 0.f);
    ASSERT_TRUE(PathTriangulator::ShouldTriangulatePath(path));
    auto rasterizer = PathRasterizer::MakeFrom(static_cast<int>(bound.width()),
                                               static_cast<int>(bound.height()), path, true);
    ASSERT_TRUE(rasterizer == nullptr);
  }

  {
    // width is zero
    Path path;
    path.moveTo(10.f, 10.f);
    path.lineTo(10.f, 110.f);
    auto bound = path.getBounds();
    ASSERT_EQ(bound.width(), 0.f);
    ASSERT_EQ(bound.height(), 100.f);
    ASSERT_TRUE(PathTriangulator::ShouldTriangulatePath(path));
    auto rasterizer = PathRasterizer::MakeFrom(static_cast<int>(bound.width()),
                                               static_cast<int>(bound.height()), path, true);
    ASSERT_TRUE(rasterizer == nullptr);
  }
}

TGFX_TEST(PathTest, ConicTo) {
  Path path;
  path.moveTo(0, 100);
  path.conicTo(100, 0, 200, 100, 0.707107f);

  std::vector<PathVerb> verbs = {};
  std::vector<Point> points = {};
  std::vector<float> weights = {};

  for (auto segment : path) {
    verbs.push_back(segment.verb);
    switch (segment.verb) {
      case PathVerb::Move:
        points.push_back(segment.points[0]);
        break;
      case PathVerb::Line:
        points.push_back(segment.points[1]);
        break;
      case PathVerb::Quad:
        points.push_back(segment.points[1]);
        points.push_back(segment.points[2]);
        break;
      case PathVerb::Conic:
        points.push_back(segment.points[1]);
        points.push_back(segment.points[2]);
        weights.push_back(segment.conicWeight);
        break;
      case PathVerb::Cubic:
        points.push_back(segment.points[1]);
        points.push_back(segment.points[2]);
        points.push_back(segment.points[3]);
        break;
      default:
        break;
    }
  }

  // Iterator returns original conic
  ASSERT_EQ(verbs.size(), 2u);
  ASSERT_EQ(verbs[0], PathVerb::Move);
  ASSERT_EQ(verbs[1], PathVerb::Conic);

  // 1 move point + 2 conic points (control + end) = 3 points
  ASSERT_EQ(points.size(), 3u);
  ASSERT_EQ(points[0].x, 0.0f);
  ASSERT_EQ(points[0].y, 100.0f);
  ASSERT_EQ(points[2].x, 200.0f);
  ASSERT_EQ(points[2].y, 100.0f);

  // Verify conic weight
  ASSERT_EQ(weights.size(), 1u);
  ASSERT_FLOAT_EQ(weights[0], 0.707107f);
}

TGFX_TEST(PathTest, ConicToQuads) {
  Point p0 = {0, 100};
  Point p1 = {100, 0};
  Point p2 = {200, 100};
  float weight = 0.707107f;

  auto quads = Path::ConvertConicToQuads(p0, p1, p2, weight);
  size_t numQuads = (quads.size() - 1) / 2;

  ASSERT_EQ(numQuads, 2u);
  ASSERT_EQ(quads[0].x, p0.x);
  ASSERT_EQ(quads[0].y, p0.y);
  ASSERT_EQ(quads[4].x, p2.x);
  ASSERT_EQ(quads[4].y, p2.y);

  ASSERT_NEAR(quads[1].x, 41.4214f, 0.001f);
  ASSERT_NEAR(quads[1].y, 58.5786f, 0.001f);
  ASSERT_NEAR(quads[2].x, 100.0f, 0.001f);
  ASSERT_NEAR(quads[2].y, 58.5786f, 0.001f);
  ASSERT_NEAR(quads[3].x, 158.5786f, 0.001f);
  ASSERT_NEAR(quads[3].y, 58.5786f, 0.001f);
}

TGFX_TEST(PathTest, Iterator) {
  // Test basic iterator functionality with various path types
  Path path;
  path.moveTo(10, 20);
  path.lineTo(30, 40);
  path.quadTo(50, 60, 70, 80);
  path.conicTo(90, 100, 110, 120, 0.707107f);
  path.cubicTo(130, 140, 150, 160, 170, 180);
  path.close();

  // Collect results using Iterator (range-for)
  std::vector<PathVerb> iteratorVerbs;
  std::vector<Point> iteratorPoints;
  std::vector<float> iteratorWeights;

  for (auto segment : path) {
    iteratorVerbs.push_back(segment.verb);

    switch (segment.verb) {
      case PathVerb::Move:
        iteratorPoints.push_back(segment.points[0]);
        break;
      case PathVerb::Line:
        iteratorPoints.push_back(segment.points[1]);
        break;
      case PathVerb::Quad:
        iteratorPoints.push_back(segment.points[1]);
        iteratorPoints.push_back(segment.points[2]);
        break;
      case PathVerb::Conic:
        iteratorPoints.push_back(segment.points[1]);
        iteratorPoints.push_back(segment.points[2]);
        iteratorWeights.push_back(segment.conicWeight);
        break;
      case PathVerb::Cubic:
        iteratorPoints.push_back(segment.points[1]);
        iteratorPoints.push_back(segment.points[2]);
        iteratorPoints.push_back(segment.points[3]);
        break;
      default:
        break;
    }
  }

  // Verify expected verb sequence (SkPath adds Line before Close for closed paths)
  ASSERT_EQ(iteratorVerbs.size(), 7u);
  ASSERT_EQ(iteratorVerbs[0], PathVerb::Move);
  ASSERT_EQ(iteratorVerbs[1], PathVerb::Line);
  ASSERT_EQ(iteratorVerbs[2], PathVerb::Quad);
  ASSERT_EQ(iteratorVerbs[3], PathVerb::Conic);
  ASSERT_EQ(iteratorVerbs[4], PathVerb::Cubic);
  ASSERT_EQ(iteratorVerbs[5], PathVerb::Line);  // Auto-added line back to start
  ASSERT_EQ(iteratorVerbs[6], PathVerb::Close);

  // Verify conic weight
  ASSERT_EQ(iteratorWeights.size(), 1u);
  ASSERT_FLOAT_EQ(iteratorWeights[0], 0.707107f);
}

TGFX_TEST(PathTest, IteratorEmptyPath) {
  // Test iterator with empty path
  Path emptyPath;

  int count = 0;
  for (auto segment : emptyPath) {
    (void)segment;
    count++;
  }

  // Empty path should have no segments
  ASSERT_EQ(count, 0);
}

TGFX_TEST(PathTest, IteratorCopy) {
  // Test iterator copy constructor and assignment
  Path path;
  path.moveTo(10, 20);
  path.lineTo(30, 40);
  path.lineTo(50, 60);

  auto iter1 = path.begin();
  ASSERT_EQ((*iter1).verb, PathVerb::Move);

  // Test copy constructor
  auto iter2 = iter1;
  ASSERT_EQ((*iter2).verb, PathVerb::Move);

  // Advance iter1, iter2 should remain at Move
  ++iter1;
  ASSERT_EQ((*iter1).verb, PathVerb::Line);
  ASSERT_EQ((*iter2).verb, PathVerb::Move);

  // Test assignment operator
  iter2 = iter1;
  ASSERT_EQ((*iter2).verb, PathVerb::Line);

  ++iter1;
  ASSERT_EQ((*iter1).verb, PathVerb::Line);
  ASSERT_EQ((*iter2).verb, PathVerb::Line);
}

TGFX_TEST(PathTest, IteratorNoConics) {
  // Test NoConicsPathIterator with conic conversion
  Path path;
  path.moveTo(0, 100);
  path.conicTo(100, 0, 200, 100, 0.707107f);

  std::vector<PathVerb> verbs;
  std::vector<Point> points;

  NoConicsPathIterator iterator(path);
  for (auto segment : iterator) {
    verbs.push_back(segment.verb);
    switch (segment.verb) {
      case PathVerb::Move:
        points.push_back(segment.points[0]);
        break;
      case PathVerb::Quad:
        points.push_back(segment.points[1]);
        points.push_back(segment.points[2]);
        break;
      default:
        break;
    }
  }

  // Conic should be converted to 2 quads (pow2=1)
  ASSERT_EQ(verbs.size(), 3u);
  ASSERT_EQ(verbs[0], PathVerb::Move);
  ASSERT_EQ(verbs[1], PathVerb::Quad);
  ASSERT_EQ(verbs[2], PathVerb::Quad);

  // 1 move point + 2*2 quad control/end points = 5 points
  ASSERT_EQ(points.size(), 5u);

  // First point is move-to
  ASSERT_EQ(points[0].x, 0.0f);
  ASSERT_EQ(points[0].y, 100.0f);

  // Last point should be the conic end point
  ASSERT_EQ(points[4].x, 200.0f);
  ASSERT_EQ(points[4].y, 100.0f);
}

TGFX_TEST(PathTest, IteratorPerformance) {
  // Build a path with many conics for performance testing
  auto buildTestPath = [](int numConics) {
    Path path;
    path.moveTo(0, 0);
    for (int i = 0; i < numConics; i++) {
      float x = static_cast<float>(i * 10);
      path.conicTo(x + 5, 10, x + 10, 0, 0.707107f);
    }
    return path;
  };

  const int iterations = 1000;
  std::vector<int> testSizes = {10, 50, 100, 500, 1000, 5000};

  printf("\n=== Path::Iterator vs SkPath::Iter Performance ===\n");
  printf("%-8s %-8s %-15s %-15s %-12s %-12s %-8s\n", "Conics", "Verbs", "Iterator(us)",
         "SkPath::Iter(us)", "ns/verb(I)", "ns/verb(S)", "Ratio");
  printf("-------------------------------------------------------------------------\n");

  for (int numConics : testSizes) {
    Path path = buildTestPath(numConics);
    auto& skPath = PathRef::ReadAccess(path);
    int verbCount = skPath.countVerbs();

    // Benchmark Path::Iterator (range-for)
    auto startIterator = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
      volatile int count = 0;
      for (const auto& segment : path) {
        if (segment.verb != PathVerb::Done) {
          count++;
        }
      }
    }
    auto endIterator = std::chrono::high_resolution_clock::now();
    double iteratorUs =
        std::chrono::duration<double, std::micro>(endIterator - startIterator).count() / iterations;

    // Benchmark SkPath::Iter directly
    auto startSkPath = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
      volatile int count = 0;
      pk::SkPath::Iter iter(skPath, false);
      pk::SkPoint pts[4];
      while (iter.next(pts) != pk::SkPath::kDone_Verb) {
        count++;
      }
    }
    auto endSkPath = std::chrono::high_resolution_clock::now();
    double skPathUs =
        std::chrono::duration<double, std::micro>(endSkPath - startSkPath).count() / iterations;

    double nsPerVerbIterator = (iteratorUs * 1000.0) / verbCount;
    double nsPerVerbSkPath = (skPathUs * 1000.0) / verbCount;
    double ratio = iteratorUs / skPathUs;

    printf("%-8d %-8d %-15.2f %-15.2f %-12.2f %-12.2f %-8.1fx\n", numConics, verbCount, iteratorUs,
           skPathUs, nsPerVerbIterator, nsPerVerbSkPath, ratio);
  }
  printf("\n");
}

TGFX_TEST(PathTest, SmoothRRectContour) {
  const auto rect = Rect::MakeWH(200, 100);
  const auto rRect = RRect::MakeRectXY(rect, 20, 20, 0.5f);

  // A smoothed corner expands to three cubics, so the path is no longer detected as an rrect or
  // an oval.
  Path path = {};
  path.addRRect(rRect);
  EXPECT_FALSE(path.isRRect(nullptr));
  EXPECT_FALSE(path.isOval());

  // moveTo + 4 lines + 12 cubics + close.
  EXPECT_EQ(path.countVerbs(), 18);
  // At full smoothness the arcs degenerate to points and are skipped, leaving 8 cubics.
  Path fullPath = {};
  fullPath.addRRect(RRect::MakeRectXY(rect, 20, 20, 1.0f));
  EXPECT_EQ(fullPath.countVerbs(), 14);

  // Corners always bulge inward from the boundary points, so the bounds equal the rect at any
  // smoothness, circular or elliptical.
  Path plainPath = {};
  plainPath.addRRect(RRect::MakeRectXY(rect, 20, 20));
  EXPECT_EQ(path.getBounds(), plainPath.getBounds());
  EXPECT_EQ(fullPath.getBounds(), plainPath.getBounds());
  Path ellipsePath = {};
  ellipsePath.addRRect(RRect::MakeRectXY(rect, 20, 40, 0.5f));
  EXPECT_EQ(ellipsePath.getBounds(), plainPath.getBounds());
}

TGFX_TEST(PathTest, SmoothRRectStartIndex) {
  const auto rRect = RRect::MakeRectXY(Rect::MakeWH(200, 100), 20, 20, 0.5f);
  // The corner footprint is (1 + 0.5) * 20 = 30 along each edge.
  const Point expected[8] = {{30, 0},    {170, 0},  {200, 30}, {200, 70},
                             {170, 100}, {30, 100}, {0, 70},   {0, 30}};
  for (unsigned startIndex = 0; startIndex < 8; ++startIndex) {
    Path path = {};
    path.addRRect(rRect, false, startIndex);
    auto iter = path.begin();
    ASSERT_NE(iter, path.end());
    ASSERT_EQ((*iter).verb, PathVerb::Move);
    EXPECT_EQ((*iter).points[0], expected[startIndex]);

    // The reversed contour starts at the same boundary point but heads into the corner (a cubic)
    // where the forward contour runs along an edge (a line), and vice versa.
    Path reversedPath = {};
    reversedPath.addRRect(rRect, true, startIndex);
    auto reversedIter = reversedPath.begin();
    ASSERT_NE(reversedIter, reversedPath.end());
    EXPECT_EQ((*reversedIter).points[0], expected[startIndex]);
    ++iter;
    ++reversedIter;
    if (startIndex % 2 == 0) {
      EXPECT_EQ((*iter).verb, PathVerb::Line);
      EXPECT_EQ((*reversedIter).verb, PathVerb::Cubic);
    } else {
      EXPECT_EQ((*iter).verb, PathVerb::Cubic);
      EXPECT_EQ((*reversedIter).verb, PathVerb::Line);
    }
  }
}

// Reference implementation of the smooth-corner SDF emitted by GLSLSmoothCorner, mirrored line
// by line so the CPU-generated contour can be validated against the same math the shader runs.
static float SmoothBezierNewtonStep(float t, const Point& d0, const Point& d1, const Point& d2,
                                    const Point& d3) {
  const auto a2 = d2 + d3 * t;
  const auto a1 = d1 + a2 * t;
  const auto b2 = a2 + d3 * t;
  const auto value = d0 + a1 * t;
  const auto deriv = a1 + b2 * t;
  const auto derivDot = std::max(deriv.x * deriv.x + deriv.y * deriv.y, 1.1755e-38f);
  return t - (deriv.x * value.x + deriv.y * value.y) / derivDot;
}

static float SmoothBezierDist(const Point& p, const Point& b0, const Point& b1, const Point& b2,
                              const Point& b3) {
  const auto d3v = (b0 * -1.0f) + b1 * 3.0f - b2 * 3.0f + b3;
  const auto d2v = b0 * 3.0f - b1 * 6.0f + b2 * 3.0f;
  const auto d1v = (b1 - b0) * 3.0f;
  const auto d0v = b0 - p;
  auto best = 1.0e38f;
  auto bestT = 0.0f;
  auto seed = 0.0f;
  for (int i = 0; i < 3; ++i) {
    auto t = seed;
    for (int j = 0; j < 2; ++j) {
      t = SmoothBezierNewtonStep(t, d0v, d1v, d2v, d3v);
    }
    t = std::clamp(t, 0.0f, 1.0f);
    const auto diff = ((d3v * t + d2v) * t + d1v) * t + d0v;
    const auto distSq = diff.x * diff.x + diff.y * diff.y;
    if (distSq < best) {
      best = distSq;
      bestT = t;
    }
    seed += 0.5f;
  }
  const auto bestDiff = ((d3v * bestT + d2v) * bestT + d1v) * bestT + d0v;
  const auto bestDeriv = (d3v * (3.0f * bestT) + d2v * 2.0f) * bestT + d1v;
  const auto side = bestDeriv.y * bestDiff.x - bestDeriv.x * bestDiff.y;
  return std::sqrt(best) * (side > 0.0f ? 1.0f : -1.0f);
}

static float SmoothCornerDistRef(const Point& q, float radius, const SmoothCornerCoeffs& coeffs) {
  const auto footprint = coeffs.tangentRatio * radius;
  const auto step = coeffs.controlStep * radius;
  const auto dx = q.x - radius;
  const auto dy = q.y - radius;
  const auto bisectorDot = (dx + dy) * (-0.70710678f);
  if (bisectorDot > 0.0f &&
      bisectorDot * bisectorDot > coeffs.arcCosSquared * (dx * dx + dy * dy)) {
    return std::sqrt(dx * dx + dy * dy) - radius;
  }
  const auto ctrl2 = footprint - 3.0f * step;
  const Point arcStart = {ctrl2 - coeffs.endOffsetX * radius, coeffs.endOffsetY * radius};
  if (q.x >= q.y) {
    return SmoothBezierDist(q, {footprint, 0.0f}, {footprint - 2.0f * step, 0.0f}, {ctrl2, 0.0f},
                            arcStart);
  }
  const Point arcEnd = {arcStart.y, arcStart.x};
  return SmoothBezierDist(q, arcEnd, {0.0f, ctrl2}, {0.0f, footprint - 2.0f * step},
                          {0.0f, footprint});
}

static Point CubicPointAt(const Point pts[4], float t) {
  const auto u = 1.0f - t;
  return pts[0] * (u * u * u) + pts[1] * (3.0f * u * u * t) + pts[2] * (3.0f * u * t * t) +
         pts[3] * (t * t * t);
}

TGFX_TEST(PathTest, SmoothRRectMatchesSDF) {
  const auto rect = Rect::MakeWH(200, 100);
  // Signs mapping device coordinates into each corner frame, in [TL, TR, BR, BL] order.
  const Point signs[4] = {{1, 1}, {-1, 1}, {-1, -1}, {1, -1}};
  const Point vertices[4] = {{0, 0}, {200, 0}, {200, 100}, {0, 100}};
  // Forward from startIndex 0 the corners appear in the order TR, BR, BL, TL.
  const size_t cornerOrder[4] = {1, 2, 3, 0};
  for (float smoothness : {0.2f, 0.5f, 0.8f, 1.0f}) {
    const auto rRect = RRect::MakeRectXY(rect, 20, 20, smoothness);
    Path path = {};
    path.addRRect(rRect);
    const auto coeffs = ComputeSmoothCornerCoeffs(rRect.smoothness());
    // At full smoothness the arc segment is skipped, leaving 2 cubics per corner instead of 3.
    const size_t cubicsPerCorner = rRect.smoothness() < 1.0f ? 3 : 2;
    size_t cubicIndex = 0;
    for (auto segment : path) {
      if (segment.verb != PathVerb::Cubic) {
        continue;
      }
      const auto corner = cornerOrder[cubicIndex / cubicsPerCorner];
      ++cubicIndex;
      for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const auto p = CubicPointAt(segment.points, t);
        const Point q = {(p.x - vertices[corner].x) * signs[corner].x,
                         (p.y - vertices[corner].y) * signs[corner].y};
        EXPECT_NEAR(SmoothCornerDistRef(q, 20, coeffs), 0.0f, 0.02f);
      }
    }
  }
}

TGFX_TEST(PathTest, SmoothCornerSDFSigns) {
  const auto coeffs = ComputeSmoothCornerCoeffs(0.5f);
  constexpr float radius = 20.0f;
  const auto footprint = coeffs.tangentRatio * radius;
  const auto step = coeffs.controlStep * radius;
  const Point arcStart = {footprint - 3.0f * step - coeffs.endOffsetX * radius,
                          coeffs.endOffsetY * radius};
  const Point center = {radius, radius};

  // Arc-cubic seam: offsetting along the radial direction must flip the sign.
  auto inward = center - arcStart;
  const auto length = std::sqrt(inward.x * inward.x + inward.y * inward.y);
  inward = inward * (0.5f / length);
  EXPECT_LT(SmoothCornerDistRef(arcStart + inward, radius, coeffs), 0.0f);
  EXPECT_GT(SmoothCornerDistRef(arcStart - inward, radius, coeffs), 0.0f);

  // Edge-cubic seam at the tangent point: the Newton search clamps to the endpoint here, and the
  // derivative-aligned edge must still give the right sign on both sides.
  const Point tangent = {footprint, 0.0f};
  EXPECT_LT(SmoothCornerDistRef(tangent + Point{0.0f, 0.5f}, radius, coeffs), 0.0f);
  EXPECT_GT(SmoothCornerDistRef(tangent - Point{0.0f, 0.5f}, radius, coeffs), 0.0f);

  // Arc sector middle: inside stays negative, outside positive.
  const Point bisector = {-0.70710678f, -0.70710678f};
  EXPECT_LT(SmoothCornerDistRef(center + bisector * (radius - 0.5f), radius, coeffs), 0.0f);
  EXPECT_GT(SmoothCornerDistRef(center + bisector * (radius + 0.5f), radius, coeffs), 0.0f);
}

}  // namespace tgfx
