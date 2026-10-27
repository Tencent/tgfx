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
#include <limits>
#include <string>
#include <vector>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#include <include/core/SkPath.h>
#pragma clang diagnostic pop
#include <include/pathops/SkPathOps.h>
#include "base/TGFXTest.h"
#include "core/NoConicsPathIterator.h"
#include "core/PathRasterizer.h"
#include "core/PathRef.h"
#include "core/PathTriangulator.h"
#include "core/ShapeRasterizer.h"
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

static Path BuildTestPath(int numConics) {
  Path path;
  path.moveTo(0, 0);
  for (int i = 0; i < numConics; i++) {
    float x = static_cast<float>(i * 10);
    path.conicTo(x + 5, 10, x + 10, 0, 0.707107f);
  }
  return path;
}

TGFX_TEST(PathTest, IteratorPerformance) {
  // Build a path with many conics for performance testing

  const int iterations = 1000;
  std::vector<int> testSizes = {10, 50, 100, 500, 1000, 5000};

  printf("\n=== Path::Iterator vs SkPath::Iter Performance ===\n");
  printf("%-8s %-8s %-15s %-15s %-12s %-12s %-8s\n", "Conics", "Verbs", "Iterator(us)",
         "SkPath::Iter(us)", "ns/verb(I)", "ns/verb(S)", "Ratio");
  printf("-------------------------------------------------------------------------\n");

  for (int numConics : testSizes) {
    Path path = BuildTestPath(numConics);
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

// Builds a square ring: an outer square with a same-direction inner square, which describes a
// counter through parity alone. addRect() emits every contour the same way round, so the result is
// only correct under the even-odd rule -- exactly the shape toWinding() exists to fix.
static Path MakeRing(const Rect& outer, const Rect& inner) {
  Path path = {};
  path.addRect(outer);
  path.addRect(inner);
  path.setFillType(PathFillType::EvenOdd);
  return path;
}

// Asserts the filled area survived the conversion, not merely that the fill type changed. A
// conversion that reported success without reorienting the contours would still pass a fill-type
// check, so the containment queries are what actually catch a silent failure.
static void ExpectRingFilledCorrectly(const Path& path, const Rect& outer, const Rect& inner,
                                      const std::string& context) {
  ASSERT_EQ(path.getFillType(), PathFillType::Winding) << context;
  auto center = Point::Make(outer.x() + outer.width() * 0.5f, outer.y() + outer.height() * 0.5f);
  auto inRing = Point::Make(outer.left + outer.width() * 0.05f, center.y);
  auto inCounter = Point::Make(inner.x() + inner.width() * 0.5f, inner.y() + inner.height() * 0.5f);
  if (outer.contains(center.x, center.y)) {
    EXPECT_FALSE(path.contains(center.x, center.y)) << context << ": counter filled in";
  }
  EXPECT_TRUE(path.contains(inRing.x, inRing.y)) << context << ": ring lost";
  EXPECT_FALSE(path.contains(inCounter.x, inCounter.y)) << context << ": counter filled in";
}

TGFX_TEST(PathTest, ToWindingReorientsSameDirectionCounters) {
  auto outer = Rect::MakeLTRB(0, 0, 1000, 1000);
  auto inner = Rect::MakeLTRB(200, 200, 800, 800);
  auto path = MakeRing(outer, inner);
  ASSERT_TRUE(path.toWinding());
  ExpectRingFilledCorrectly(path, outer, inner, "ring");
}

// 回: three levels of nesting -- filled, hole, filled. The winding directions have to alternate, so
// a conversion that only fixed the outermost hole would leave the island filled in.
static Path MakeNestedCounter() {
  Path path = {};
  path.addRect(Rect::MakeLTRB(0, 0, 1000, 1000));
  path.addRect(Rect::MakeLTRB(150, 150, 850, 850));
  path.addRect(Rect::MakeLTRB(300, 300, 700, 700));
  path.setFillType(PathFillType::EvenOdd);
  return path;
}

// 品: three rings side by side, each keeping its own counter, so the conversion has to run per
// contour rather than flipping everything at once. The rectangles come back with the path so a
// caller can probe each counter without rebuilding the geometry.
struct IslandsPath {
  Path path = {};
  std::vector<Rect> outers = {};
  std::vector<Rect> inners = {};
};

static IslandsPath MakeIslands() {
  IslandsPath islands = {};
  for (int index = 0; index < 3; index++) {
    auto left = static_cast<float>(index) * 400.0f;
    auto outer = Rect::MakeLTRB(left, 0.0f, left + 380.0f, 380.0f);
    auto inner = Rect::MakeLTRB(left + 80.0f, 80.0f, left + 300.0f, 300.0f);
    islands.outers.push_back(outer);
    islands.inners.push_back(inner);
    islands.path.addRect(outer);
    islands.path.addRect(inner);
  }
  islands.path.setFillType(PathFillType::EvenOdd);
  return islands;
}

TGFX_TEST(PathTest, ToWindingHandlesMultipleNestingLevels) {
  auto path = MakeNestedCounter();
  ASSERT_TRUE(path.toWinding());
  ASSERT_EQ(path.getFillType(), PathFillType::Winding);
  EXPECT_TRUE(path.contains(75, 500)) << "outer ring lost";
  EXPECT_FALSE(path.contains(220, 500)) << "counter filled in";
  EXPECT_TRUE(path.contains(500, 500)) << "island lost";
}

TGFX_TEST(PathTest, ToWindingKeepsSeparateIslands) {
  auto islands = MakeIslands();
  auto path = islands.path;
  ASSERT_TRUE(path.toWinding());
  ASSERT_EQ(path.getFillType(), PathFillType::Winding);
  for (size_t index = 0; index < islands.outers.size(); index++) {
    ExpectRingFilledCorrectly(path, islands.outers[index], islands.inners[index],
                              "island " + std::to_string(index));
  }
}

TGFX_TEST(PathTest, ToWindingHandlesCurvedNesting) {
  auto outer = Rect::MakeLTRB(0, 0, 1000, 1000);
  auto inner = Rect::MakeLTRB(250, 250, 750, 750);
  Path path = {};
  path.addOval(outer);
  path.addOval(inner);
  path.setFillType(PathFillType::EvenOdd);
  ASSERT_TRUE(path.toWinding());
  ASSERT_EQ(path.getFillType(), PathFillType::Winding);
  EXPECT_TRUE(path.contains(80, 500)) << "ring lost";
  EXPECT_FALSE(path.contains(500, 500)) << "counter filled in";
}

TGFX_TEST(PathTest, ToWindingLeavesWindingPathsUnchanged) {
  auto outer = Rect::MakeLTRB(0, 0, 1000, 1000);
  auto inner = Rect::MakeLTRB(200, 200, 800, 800);
  auto path = MakeRing(outer, inner);
  path.setFillType(PathFillType::Winding);
  auto before = path;
  ASSERT_TRUE(path.toWinding());
  EXPECT_EQ(path.getFillType(), PathFillType::Winding);
  EXPECT_TRUE(path == before) << "a winding path must be left alone";
}

TGFX_TEST(PathTest, ToWindingMapsInverseFillType) {
  auto outer = Rect::MakeLTRB(0, 0, 1000, 1000);
  auto inner = Rect::MakeLTRB(200, 200, 800, 800);
  auto path = MakeRing(outer, inner);
  path.setFillType(PathFillType::InverseEvenOdd);
  ASSERT_TRUE(path.toWinding());
  EXPECT_EQ(path.getFillType(), PathFillType::InverseWinding);
  // Everything outside the outer square is filled when the fill type is inverse.
  EXPECT_TRUE(path.contains(2000, 2000));
}

TGFX_TEST(PathTest, ToWindingAcceptsEmptyAndSingleContourPaths) {
  Path empty = {};
  empty.setFillType(PathFillType::EvenOdd);
  EXPECT_TRUE(empty.toWinding());
  EXPECT_TRUE(empty.isEmpty());
  EXPECT_EQ(empty.getFillType(), PathFillType::Winding);

  Path single = {};
  single.addRect(Rect::MakeLTRB(0, 0, 100, 100));
  single.setFillType(PathFillType::EvenOdd);
  EXPECT_TRUE(single.toWinding());
  EXPECT_EQ(single.getFillType(), PathFillType::Winding);
  EXPECT_TRUE(single.contains(50, 50));
}

// toWinding() leaves a path that already uses winding untouched, so whether the input actually
// exhibits the bug has to be measured on a copy. Without this check a conversion that did nothing
// and one that worked would look identical whenever the input stopped showing the bug.
static bool FilledUnderWinding(const Path& path, float x, float y) {
  auto probe = path;
  probe.setFillType(PathFillType::Winding);
  return probe.contains(x, y);
}

static void AddCubicCircle(Path& path, float cx, float cy, float r) {
  const float k = 0.5522847498f;
  path.moveTo(cx + r, cy);
  path.cubicTo(cx + r, cy + k * r, cx + k * r, cy + r, cx, cy + r);
  path.cubicTo(cx - k * r, cy + r, cx - r, cy + k * r, cx - r, cy);
  path.cubicTo(cx - r, cy - k * r, cx - k * r, cy - r, cx, cy - r);
  path.cubicTo(cx + k * r, cy - r, cx + r, cy - k * r, cx + r, cy);
  path.close();
}

TGFX_TEST(PathTest, ToWindingOrientsCurvedCounters) {
  // Counters bounded by curves: written the way a real font writes them (cubic) and the way tgfx
  // builds them (conic). Both run the same way as the outline around them, so only parity keeps
  // them open, and the flattened area still has to carry the right sign.
  {
    Path path = {};
    AddCubicCircle(path, 500.0f, 500.0f, 500.0f);
    AddCubicCircle(path, 500.0f, 500.0f, 250.0f);
    path.setFillType(PathFillType::EvenOdd);
    ASSERT_TRUE(FilledUnderWinding(path, 500.0f, 500.0f)) << "test input no longer shows the bug";
    ASSERT_TRUE(path.toWinding());
    EXPECT_EQ(path.getFillType(), PathFillType::Winding);
    EXPECT_TRUE(path.contains(500.0f, 20.0f)) << "ring lost";
    EXPECT_FALSE(path.contains(500.0f, 500.0f)) << "counter filled in";
  }
  {
    Path path = {};
    path.addOval(Rect::MakeLTRB(0.0f, 0.0f, 1000.0f, 1000.0f));
    path.addOval(Rect::MakeLTRB(250.0f, 250.0f, 750.0f, 750.0f));
    path.setFillType(PathFillType::EvenOdd);
    ASSERT_TRUE(FilledUnderWinding(path, 500.0f, 500.0f)) << "test input no longer shows the bug";
    ASSERT_TRUE(path.toWinding());
    EXPECT_EQ(path.getFillType(), PathFillType::Winding);
    EXPECT_TRUE(path.contains(500.0f, 20.0f)) << "ring lost";
    EXPECT_FALSE(path.contains(500.0f, 500.0f)) << "counter filled in";
  }
}

TGFX_TEST(PathTest, ToWindingKeepsOrientationFarFromOrigin) {
  // Coordinates far from the origin make the cross products feeding the area far larger than the
  // area itself, so in single precision the difference cancels out and the sign -- and with it the
  // direction every contour is turned -- becomes noise.
  const float origin = 1e7f;
  const float outer = 1000.0f;
  const float inner = 500.0f;
  Path path = {};
  path.addRect(Rect::MakeLTRB(origin - outer, origin - outer, origin + outer, origin + outer));
  path.addRect(Rect::MakeLTRB(origin - inner, origin - inner, origin + inner, origin + inner));
  path.setFillType(PathFillType::EvenOdd);
  ASSERT_TRUE(FilledUnderWinding(path, origin, origin)) << "test input no longer shows the bug";
  ASSERT_TRUE(path.toWinding());
  EXPECT_EQ(path.getFillType(), PathFillType::Winding);
  EXPECT_TRUE(path.contains(origin, origin - outer + 10.0f)) << "ring lost";
  EXPECT_FALSE(path.contains(origin, origin)) << "counter filled in";
}

TGFX_TEST(PathTest, ToWindingLeavesCrossingContoursAlone) {
  // Contours that share area are neither nested nor disjoint. Real glyphs do this: two strokes of
  // one character overlap where they meet. Neither is flipped, so both stay outermost and keep the
  // direction they were drawn with. That is still not enough to keep the even-odd area when they
  // run the same way: even-odd reads an even crossing count in the intersection and leaves it
  // empty, while two winding numbers of the same sign add up to a non-zero one and fill it. No
  // choice of directions can change that, so it is asserted here as the known deviation from the
  // contract in the header, which holds for outlines whose contours do not cross. Keeping the
  // even-odd area of these takes a boolean operation - addPath() runs one and orients the result.
  Path path = {};
  path.addRect(Rect::MakeLTRB(0.0f, 0.0f, 600.0f, 600.0f));
  path.addRect(Rect::MakeLTRB(400.0f, 400.0f, 1000.0f, 1000.0f));
  path.setFillType(PathFillType::EvenOdd);
  ASSERT_FALSE(path.contains(500.0f, 500.0f)) << "input no longer shows the deviation";
  ASSERT_TRUE(path.toWinding());
  EXPECT_EQ(path.getFillType(), PathFillType::Winding);
  EXPECT_TRUE(path.contains(100.0f, 100.0f)) << "first contour lost";
  EXPECT_TRUE(path.contains(900.0f, 900.0f)) << "second contour lost";
  EXPECT_TRUE(path.contains(500.0f, 500.0f)) << "documented deviation: the intersection fills in";
}

TGFX_TEST(PathTest, ToWindingKeepsAreaOfOppositeOverlappingContours) {
  // The other way round the area can be kept. Two outermost contours that overlap in opposite
  // directions leave their intersection empty under even-odd, and under winding as well, because
  // the two winding numbers cancel there. That only holds while each keeps the direction it was
  // drawn with: forcing every outermost contour to one shared direction - which neither rule asks
  // for, winding only wanting a non-zero sum - makes them agree and fills the intersection in.
  Path path = {};
  path.addRect(Rect::MakeLTRB(0.0f, 0.0f, 600.0f, 600.0f));
  path.addRect(Rect::MakeLTRB(400.0f, 400.0f, 1000.0f, 1000.0f), true);
  path.setFillType(PathFillType::EvenOdd);
  ASSERT_FALSE(path.contains(500.0f, 500.0f)) << "input no longer shows the case";
  ASSERT_TRUE(path.toWinding());
  EXPECT_EQ(path.getFillType(), PathFillType::Winding);
  EXPECT_FALSE(path.contains(500.0f, 500.0f)) << "intersection filled in";
  EXPECT_TRUE(path.contains(100.0f, 100.0f)) << "first contour lost";
  EXPECT_TRUE(path.contains(900.0f, 900.0f)) << "second contour lost";
}

namespace {

// The band between f(x) = 2x - x^2/512 and (4095/4096) * f(x): about a tenth of a unit thick at
// its widest while spanning 512 units. The two sides are deliberately split differently - one
// quadratic for the upper edge, two for the lower one - because with matching splits the sampling
// errors scale together and the sign never flips. With different splits the sampled area comes out
// with the opposite sign to the true area, which is what made the old implementation flip the
// counter and fill it in.
void AddThinCounter(Path* path) {
  path->moveTo(0.0f, 0.0f);
  path->quadTo(512.0f, 1024.0f, 1024.0f, 0.0f);
  path->quadTo(768.0f, 511.875f, 512.0f, 511.875f);
  path->quadTo(256.0f, 511.875f, 0.0f, 0.0f);
  path->close();
}

}  // namespace

TGFX_TEST(PathTest, ToWindingKeepsThinCurveCounter) {
  Path path = {};
  path.addRect(Rect::MakeLTRB(0.0f, 0.0f, 1100.0f, 1100.0f));
  AddThinCounter(&path);
  path.setFillType(PathFillType::EvenOdd);
  ASSERT_TRUE(path.toWinding());
  EXPECT_EQ(path.getFillType(), PathFillType::Winding);
  // The middle of the band: the counter has to stay a counter.
  EXPECT_FALSE(path.contains(512.0f, 511.9375f)) << "thin counter filled in";
  // Well inside the outer rectangle, which the thin contour does not enclose.
  EXPECT_TRUE(path.contains(512.0f, 300.0f)) << "the outline was lost";
}

TGFX_TEST(PathTest, ToWindingKeepsConicCounter) {
  // Two nested ovals: addOval() emits conics, so this is the conic path through the conversion.
  // The geometry is an unambiguous ring rather than a thin band, which keeps the expected result
  // free of any sampling question: the inner oval has to stay a counter.
  Path path = {};
  path.addOval(Rect::MakeLTRB(0.0f, 0.0f, 1000.0f, 1000.0f));
  path.addOval(Rect::MakeLTRB(200.0f, 200.0f, 800.0f, 800.0f));
  path.setFillType(PathFillType::EvenOdd);
  ASSERT_TRUE(path.toWinding());
  EXPECT_EQ(path.getFillType(), PathFillType::Winding);
  EXPECT_FALSE(path.contains(500.0f, 500.0f)) << "conic counter filled in";
  EXPECT_TRUE(path.contains(500.0f, 100.0f)) << "the outline was lost";
}

TGFX_TEST(PathTest, ToWindingKeepsThinMixedConicCounter) {
  // A counter about one unit thick whose two sides are not the same kind of curve: a conic along
  // the top, a quad along the bottom. Its area is a small difference between two nearly equal arcs,
  // which is exactly where a chord approximation that misses both arcs on the same side would flip
  // the contour and fill the sliver in.
  Path path = {};
  path.addRect(Rect::MakeLTRB(0.0f, 0.0f, 1000.0f, 1000.0f));
  path.moveTo(400.0f, 400.0f);
  // A weight of exactly 1 is rewritten as a quad by the underlying path, which would send this
  // outline down the quad branch and leave the conic bound untested. Keep the weight off 1.
  path.conicTo(500.0f, 401.0f, 600.0f, 400.0f, 2.0f);
  path.quadTo(500.0f, 398.5f, 400.0f, 400.0f);
  path.close();
  int conicCount = 0;
  for (const auto& segment : path) {
    if (segment.verb == PathVerb::Conic) {
      conicCount++;
    }
  }
  ASSERT_EQ(conicCount, 1) << "the outline has to really reach the conic branch";
  path.setFillType(PathFillType::EvenOdd);
  ASSERT_TRUE(path.toWinding());
  EXPECT_EQ(path.getFillType(), PathFillType::Winding);
  EXPECT_FALSE(path.contains(500.0f, 400.0f)) << "thin mixed counter filled in";
  EXPECT_TRUE(path.contains(500.0f, 300.0f)) << "the outline was lost";
}

TGFX_TEST(PathTest, ToWindingRejectsUndecidableConicDirection) {
  // The same conic traced out and back, so the two arcs cancel. No amount of refinement can put the
  // error bound below an area of zero, so the direction has to be reported as undecidable rather
  // than guessed, and the path has to come back untouched - a silent failure would leave an outline
  // that still depends on the even-odd rule while claiming to be winding.
  Path path = {};
  path.moveTo(0.0f, 0.0f);
  path.conicTo(20.0f, 100.0f, 40.0f, 0.0f, 2.0f);
  path.conicTo(20.0f, 100.0f, 0.0f, 0.0f, 2.0f);
  path.setFillType(PathFillType::EvenOdd);
  auto before = path;
  EXPECT_FALSE(path.toWinding());
  EXPECT_TRUE(path == before) << "a failed conversion must leave the path unchanged";
  EXPECT_EQ(path.getFillType(), PathFillType::EvenOdd);
}

TGFX_TEST(PathTest, ToWindingCountsTheImplicitClosingEdge) {
  // An outline that never calls close() still fills as if it did, so the gap back to its start is
  // an edge and contributes to the area. Dropping it makes this counter look like it already runs
  // against the outline around it, so it is left alone and its interior turns solid.
  Path path = {};
  path.moveTo(0.0f, 0.0f);
  path.lineTo(10.0f, 0.0f);
  path.lineTo(0.0f, 10.0f);
  path.close();
  path.moveTo(2.0f, 6.0f);
  path.lineTo(2.0f, 2.0f);
  path.lineTo(6.0f, 2.0f);
  path.setFillType(PathFillType::EvenOdd);
  ASSERT_TRUE(path.toWinding());
  EXPECT_EQ(path.getFillType(), PathFillType::Winding);
  EXPECT_FALSE(path.contains(3.0f, 3.0f)) << "the unclosed counter was filled in";
  EXPECT_TRUE(path.contains(1.0f, 1.0f)) << "the outline was lost";
}

TGFX_TEST(PathTest, ToWindingRejectsNonFiniteCoordinates) {
  // Neither an area nor a containment test means anything here. Guessing a direction would hand
  // back a path that claims a winding rule its geometry does not follow, so this has to fail.
  Path path = {};
  path.moveTo(0.0f, 0.0f);
  path.cubicTo(std::numeric_limits<float>::infinity(), 0.0f, 100.0f, 100.0f, 0.0f, 100.0f);
  path.close();
  path.moveTo(200.0f, 200.0f);
  path.lineTo(400.0f, 200.0f);
  path.lineTo(400.0f, 400.0f);
  path.close();
  path.setFillType(PathFillType::EvenOdd);
  EXPECT_FALSE(path.toWinding());
  EXPECT_EQ(path.getFillType(), PathFillType::EvenOdd) << "a failure must leave the path alone";
}

}  // namespace tgfx
