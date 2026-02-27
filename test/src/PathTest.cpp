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
    // infinite loop path, return count 0
    ASSERT_FALSE(PathTriangulator::ShouldTriangulatePath(*path));
    auto bounds = path->getBounds();
    std::vector<float> vertices = {};
    auto count = PathTriangulator::ToAATriangles(*path, bounds, &vertices);
    ASSERT_EQ(count, 0u);
  }

  {
    // scale up 100, succeed
    path->transform(Matrix::MakeScale(100, 100));
    ASSERT_TRUE(PathTriangulator::ShouldTriangulatePath(*path));
    auto bounds = path->getBounds();
    std::vector<float> vertices = {};
    auto count = PathTriangulator::ToAATriangles(*path, bounds, &vertices);
    ASSERT_EQ(count, 120u);
  }

  {
    // scale down to 0.5, succeed
    path->transform(Matrix::MakeScale(0.005f, 0.005f));
    ASSERT_FALSE(PathTriangulator::ShouldTriangulatePath(*path));
    auto bounds = path->getBounds();
    std::vector<float> vertices = {};
    auto count = PathTriangulator::ToAATriangles(*path, bounds, &vertices);
    ASSERT_EQ(count, 402u);
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

}  // namespace tgfx
