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

// Intersects two SVG paths and renders the result.
TGFX_TEST(PathTest, TwoPathsIntersect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  // Parse first SVG path (a circle)
  auto pathString =
      "M46.3634 34.8678Q46.4404 33.8572 46.423 32.7242L-45.5661 34.1372Q-45.6133 31.0643 -45.3708 "
      "27.8805L46.3634 34.8678ZM46.0385 28.3945Q45.8032 26.9061 45.4308 25.4111Q44.8612 23.1236 "
      "44.0071 20.8737L-42.0044 53.5237Q-43.0907 50.6621 -43.8425 47.6434Q-44.4408 45.2406 "
      "-44.8328 42.7615L46.0385 28.3945ZM35.882 7.5493Q32.0687 3.2775 27.2734 0.1105Q22.203 "
      "-3.2383 16.4104 -5.1015L-11.7612 82.4791Q-17.9747 80.4804 -23.4274 76.8792Q-28.6049 73.4598 "
      "-32.7505 68.8157L35.882 7.5493ZM-5.6804 -6.4003Q-9.6481 -5.6157 -13.3688 -4.1227Q-15.4926 "
      "-3.2703 -17.4804 -2.21L25.8198 78.9632Q23.4038 80.252 20.8929 81.2597Q16.6349 82.9683 "
      "12.1687 83.8516L-5.6804 -6.4003ZM-22.1036 0.6858Q-22.7511 1.1609 -23.3447 1.6426Q-23.6579 "
      "1.8967 -23.9123 2.1146L35.9346 71.9884Q35.2612 72.5653 34.6245 73.0819Q33.479 74.0114 "
      "32.3208 74.8612L-22.1036 0.6858ZM-23.8129 2.0223Q-23.6404 1.8663 -23.4192 1.6632Q-22.6187 "
      "0.9279 -21.8791 0.2693L39.3048 68.9754Q39.0936 69.1635 38.8141 69.4202Q38.3298 69.865 "
      "37.8928 70.2602L-23.8129 2.0223ZM-18.4548 -2.6406Q-17.2806 -3.5841 -16.1409 "
      "-4.4163Q-14.2198 -5.8191 -12.2471 -7.0349L36.0226 71.2852Q37.1214 70.608 38.1139 "
      "69.8832Q38.6888 69.4634 39.1695 69.0771L-18.4548 -2.6406ZM-1.2696 -12.2288Q2.5956 -13.5418 "
      "6.5773 -14.2166Q14.7638 -15.6041 22.9197 -14.2483L7.8335 76.5064Q14.9112 77.6829 21.9503 "
      "76.4899Q25.2219 75.9354 28.3224 74.8822L-1.2696 -12.2288ZM49.6901 -0.5218Q54.4426 4.1637 "
      "57.9907 10.0685L-20.8676 57.4538Q-18.4275 61.5146 -14.9008 64.9917L49.6901 -0.5218ZM62.2368 "
      "18.6301Q62.9748 20.4581 63.6273 22.3876Q62.8919 20.2126 61.9034 18.0961L-21.4545 "
      "57.0249Q-22.6259 54.5166 -23.5251 51.8571Q-23.3276 52.4414 -23.0721 53.074L62.2368 "
      "18.6301ZM51.9936 4.5097Q46.856 -0.2793 40.4874 -3.284Q32.464 -7.0697 23.5759 -7.532L18.7975 "
      "84.3439Q9.5917 83.8651 1.2312 79.9203Q-5.3776 76.8022 -10.7364 71.8071L51.9936 "
      "4.5097ZM-10.1019 4.9981Q-13.9781 8.7258 -16.8259 13.2292Q-18.9115 16.5274 -20.3298 "
      "20.0548L65.0285 54.3763Q63.3224 58.6193 60.9321 62.3994Q57.8244 67.3138 53.6691 "
      "71.3099L-10.1019 4.9981ZM-22.0872 25.6741Q-22.1811 26.1117 -22.2362 26.4416Q-22.2453 "
      "26.4964 -22.2516 26.5385L68.7473 40.0739Q68.633 40.8423 68.5073 41.5947Q68.2158 43.3407 "
      "67.8684 44.9609L-22.0872 25.6741ZM-22.2462 26.4913Q-22.2296 26.3487 -22.2124 "
      "26.1524L69.4343 34.2079Q69.3044 35.6858 69.1394 37.1062L-22.2462 26.4913ZM-22.1676 "
      "25.5504Q-22.1556 25.3614 -22.1451 25.1586Q-22.1337 24.938 -22.125 24.7193L69.8023 "
      "28.3782Q69.7714 29.1529 69.7312 29.9283Q69.6939 30.6455 69.6494 31.3499L-22.1676 "
      "25.5504ZM-22.1012 23.8605Q-22.0947 23.4401 -22.0986 23.0249L69.8974 22.1667Q69.9118 23.718 "
      "69.888 25.2738L-22.1012 23.8605ZM-22.1163 22.211Q-22.1232 22.0108 -22.1325 21.8114Q-22.1429 "
      "21.5899 -22.1554 21.3823L69.6783 15.8524Q69.7276 16.6715 69.7667 17.505Q69.803 18.2806 "
      "69.8297 19.0587L-22.1163 22.211ZM-22.205 20.7083Q-22.2258 20.4751 -22.2477 20.2953L69.0773 "
      "9.1713Q69.2784 10.8217 69.4309 12.5306L-22.205 20.7083ZM-22.265 20.165Q-22.2549 20.2284 "
      "-22.2324 20.3409L67.9717 2.2517Q68.3165 3.971 68.5992 5.7529L-22.265 20.165ZM-22.0599 "
      "21.072Q-21.8865 21.7269 -21.6397 22.4435L65.3487 -7.5066Q66.1919 -5.0577 66.8755 "
      "-2.4761L-22.0599 21.072ZM-20.8285 24.4974Q-20.5428 25.1411 -20.2181 25.7855Q-19.3803 "
      "27.4484 -18.3175 29.067L58.586 -21.4289Q60.4129 -18.6467 61.9436 -15.6083Q62.6313 -14.2434 "
      "63.2584 -12.8308L-20.8285 24.4974ZM-9.7107 38.2743Q-5.9984 41.1266 -1.6968 43.0601Q3.3453 "
      "45.3266 8.817 46.1659L22.7663 -44.7704Q29.6569 -43.7134 36.0217 -40.8524Q41.5268 -38.3779 "
      "46.3405 -34.6795L-9.7107 38.2743ZM26.2145 45.0019Q28.96 44.1789 31.4681 42.9769L31.4359 "
      "42.9923L-8.3198 -39.9744L-8.2919 -39.9878Q-4.3288 -41.8871 -0.2004 -43.1245L26.2145 "
      "45.0019ZM31.3759 43.0211L31.3458 43.0355L11.4679 1.5522L49.3554 27.6393L49.3365 "
      "27.6667L-26.4384 -24.5076L-19.4665 -34.6332L-8.3799 -39.9456L31.3759 43.0211ZM49.2987 "
      "27.7216L49.2798 27.749Q49.4541 27.496 49.6051 27.2621L-27.6759 -22.6543Q-27.0909 -23.56 "
      "-26.4951 -24.4253L-26.4762 -24.4527L49.2987 27.7216ZM49.8655 26.8422Q49.8369 26.8915 "
      "49.7558 27.0443L-31.4945 -16.1112Q-30.6094 -17.7776 -29.721 -19.3096L49.8655 "
      "26.8422ZM49.3875 27.767Q49.2626 28.02 49.1284 28.3008Q49.0311 28.5042 48.9357 "
      "28.7085L-34.421 -10.223Q-34.1512 -10.8006 -33.8765 -11.3753Q-33.4908 -12.1821 -33.1079 "
      "-12.9579L49.3875 27.767ZM48.5622 29.5273Q48.3793 29.9379 48.204 30.3515L-36.4951 "
      "-5.5661Q-35.9975 -6.7395 -35.4789 -7.9038L48.5622 29.5273ZM47.8607 31.1817Q47.6927 31.5984 "
      "47.532 32.0187L-38.4008 -0.8379Q-37.9455 -2.0286 -37.4691 -3.2106L47.8607 31.1817ZM47.2177 "
      "32.8632Q47.141 33.0753 47.0659 33.2885Q46.9846 33.5198 46.9086 33.7434L-40.1991 "
      "4.1417Q-39.9639 3.4499 -39.7182 2.7515Q-39.5067 2.1505 -39.2898 1.5511L47.2177 "
      "32.8632ZM46.6461 34.5443Q46.5391 34.8845 46.451 35.1908L-41.9686 9.7751Q-41.5651 8.3712 "
      "-41.1153 6.9412L46.6461 34.5443ZM46.3193 35.6717Q46.2758 35.84 46.2454 35.9738L-43.4688 "
      "15.5937Q-43.138 14.1371 -42.755 12.6549L46.3193 35.6717ZM46.2219 36.0827Q46.2233 36.0755 "
      "46.2261 36.0597Q46.2406 35.9773 46.2567 35.8668L-44.7847 22.6203Q-44.6062 21.3936 -44.3868 "
      "20.1444Q-44.2523 19.3783 -44.1028 18.6058L46.2219 36.0827ZM-41.4713 52.0766Q-42.2763 49.794 "
      "-42.872 47.4021Q-43.4774 44.9711 -43.8679 42.4618L47.0378 28.3147Q46.7935 26.7447 46.4011 "
      "25.169Q45.9405 23.3194 45.2918 21.4798L-41.4713 52.0766ZM-44.5708 33.862Q-44.6046 30.8754 "
      "-44.3608 27.7796L47.3552 35.0036Q47.4363 33.9731 47.4233 32.821L-44.5708 33.862ZM-43.786 "
      "22.7011Q-43.6132 21.5204 -43.4019 20.3176Q-43.2682 19.5564 -43.1198 18.7895L47.2039 "
      "36.272Q47.2068 36.2569 47.2111 36.2325Q47.2267 36.1437 47.2437 36.0271L-43.786 "
      "22.7011ZM-42.4904 15.8006Q-42.1622 14.3567 -41.7822 12.8874L47.2881 35.9194Q47.2482 36.0739 "
      "47.2209 36.1936L-42.4904 15.8006ZM-41.0028 10.0347Q-40.6031 8.6449 -40.1576 7.2292L47.6008 "
      "34.842Q47.4975 35.1702 47.4129 35.4643L-41.0028 10.0347ZM-39.2507 4.4588Q-39.0181 3.7744 "
      "-38.775 3.0836Q-38.5649 2.4864 -38.3495 1.8914L48.158 33.2036Q48.0827 33.4117 48.0093 "
      "33.6203Q47.9296 33.8468 47.8557 34.0642L-39.2507 4.4588ZM-37.4667 -0.4808Q-37.0147 -1.663 "
      "-36.5416 -2.8367L48.7881 31.5555Q48.6235 31.964 48.466 32.3759L-37.4667 -0.4808ZM-35.5744 "
      "-5.1757Q-35.0803 -6.3408 -34.5654 -7.497L49.4757 29.9342Q49.2965 30.3365 49.1246 "
      "30.7419L-35.5744 -5.1757ZM-33.5149 -9.7998Q-33.2472 -10.373 -32.9742 -10.9441Q-32.6029 "
      "-11.7209 -32.2344 -12.4682L50.2782 28.2218Q50.1589 28.4637 50.0305 28.7322Q49.9353 28.9315 "
      "49.8418 29.1317L-33.5149 -9.7998ZM-30.6833 -15.5067Q-29.8334 -17.1124 -28.9817 "
      "-18.5906L50.7341 27.3375Q50.7069 27.3847 50.6296 27.5307L-30.6833 -15.5067ZM-27.0252 "
      "-21.8172Q-26.4668 -22.6906 -25.8989 -23.5258L-18.743 -34.0495L-7.1958 -39.3989Q-3.0864 "
      "-41.3026 1.2003 -42.4836L25.6364 46.2118Q28.6833 45.3724 31.4761 44.0786L12.1402 "
      "2.3398L50.1792 28.2054Q50.3438 27.9634 50.487 27.7395L-27.0252 -21.8172ZM23.8804 "
      "-43.557Q35.6728 -41.4379 45.3062 -34.1992L-9.9605 39.3508Q-2.1165 45.2449 7.608 "
      "46.9925L23.8804 -43.557ZM57.5518 -21.1784Q59.463 -18.3096 61.0506 -15.1583Q61.7419 -13.7863 "
      "62.371 -12.3654L-21.7512 24.8833Q-21.4518 25.5593 -21.1112 26.2353Q-20.192 28.0598 -19.0141 "
      "29.8279L57.5518 -21.1784ZM64.4533 -7.0377Q65.2863 -4.6018 65.9596 -2.0325L-23.0352 "
      "21.2897Q-22.8546 21.9789 -22.5977 22.73L64.4533 -7.0377ZM67.0291 2.6312Q67.3631 4.3163 "
      "67.6369 6.0632L-23.2534 20.3097Q-23.2412 20.3874 -23.2161 20.5138L67.0291 2.6312ZM68.0995 "
      "9.4087Q68.2939 11.0221 68.4417 12.6929L-23.2006 20.7984Q-23.22 20.5792 -23.24 "
      "20.413L68.0995 9.4087ZM68.6817 15.9379Q68.7296 16.738 68.7678 17.5519Q68.8038 18.3218 "
      "68.8303 19.0934L-23.1157 22.2453Q-23.1224 22.0512 -23.1314 21.8581Q-23.1414 21.6443 "
      "-23.1534 21.4439L68.6817 15.9379ZM68.8974 22.1767Q68.9118 23.7152 68.8881 25.259L-23.1011 "
      "23.8455Q-23.0948 23.4375 -23.0986 23.0346L68.8974 22.1767ZM68.8031 28.3385Q68.7725 29.1074 "
      "68.7325 29.8766Q68.6957 30.5864 68.6518 31.2818L-23.1655 25.4864Q-23.1539 25.3032 -23.1437 "
      "25.1066Q-23.1326 24.8925 -23.1242 24.68L68.8031 28.3385ZM68.4392 34.1084Q68.3107 35.5718 "
      "68.1476 36.9776L-23.2393 26.3749Q-23.2244 26.2459 -23.2083 26.0632L68.4392 34.1084ZM67.7592 "
      "39.9199Q67.6458 40.6825 67.5211 41.4296Q67.2557 43.019 66.9485 44.4855L-23.0974 "
      "25.6245Q-23.177 26.0045 -23.2225 26.2772Q-23.2331 26.3405 -23.2406 26.391L67.7592 "
      "39.9199ZM64.3412 53.4044Q62.6024 57.8869 60.0869 61.8648Q59.9178 62.1319 59.7652 "
      "62.3666L17.3544 127.6183L-19.3624 59.001Q-20.1239 57.578 -20.7913 56.0956L63.0979 "
      "18.3252Q62.4754 16.9427 61.7546 15.5956L21.1961 37.2983L-17.3731 12.23Q-17.5142 12.4472 "
      "-17.6711 12.6949Q-19.9357 16.2762 -21.4315 20.132L64.3412 53.4044ZM-22.1796 "
      "52.6727Q-22.3829 52.1132 -22.5775 51.5377Q-22.4585 51.8897 -22.3161 52.264L63.6714 "
      "19.551Q64.142 20.7879 64.5743 22.0664Q64.438 21.6633 64.2885 21.2518L-22.1796 "
      "52.6727ZM-21.3631 54.4543Q-20.4194 56.3917 -19.1553 58.2903L57.423 7.3024Q59.5838 10.5476 "
      "61.3462 14.1658L-21.3631 54.4543ZM-14.2339 64.2354Q-9.2578 69.1462 -2.8112 72.1548L36.0969 "
      "-11.2128Q44.0523 -7.4999 50.3884 -1.247L-14.2339 64.2354ZM13.869 76.07Q17.8671 76.1677 "
      "21.7834 75.5039Q24.867 74.9813 27.7938 74.0056L-1.3023 -13.2722Q2.5001 -14.5398 6.4099 "
      "-15.2025Q11.2434 -16.0217 16.1174 -15.9025L13.869 76.07ZM35.3889 70.5013Q36.5118 69.8148 "
      "37.5241 69.0757Q38.0775 68.6715 38.5377 68.3022L-19.0473 -3.4472Q-17.8715 -4.3908 -16.7306 "
      "-5.224Q-14.6946 -6.7106 -12.5974 -7.9928L35.3889 70.5013ZM38.64 68.2285Q38.4226 68.422 "
      "38.1374 68.6839Q37.6615 69.121 37.2326 69.509L-24.4829 1.28Q-24.3134 1.1267 -24.0955 "
      "0.9265Q-23.2803 0.1778 -22.5254 -0.4941L38.64 68.2285ZM35.3036 71.2123Q34.6309 71.7888 "
      "33.9952 72.3047Q32.9403 73.1607 31.8794 73.9448L-22.8021 -0.0412Q-23.4139 0.411 -23.9757 "
      "0.8668Q-24.2996 1.1297 -24.5651 1.3572L35.3036 71.2123ZM25.6286 77.9314Q23.1278 79.2853 "
      "20.5207 80.3315Q17.1537 81.6825 13.6462 82.5077L-7.4227 -7.0473Q-10.6687 -6.2836 -13.7415 "
      "-5.0506Q-16.0317 -4.1316 -18.1715 -2.9732L25.6286 77.9314ZM-9.0295 82.2317Q-16.4652 80.2791 "
      "-22.8772 76.0441Q-28.8826 72.078 -33.4377 66.4711L37.9677 8.4602Q33.6112 3.0977 27.8255 "
      "-0.7233Q21.5771 -4.8502 14.3377 -6.7513L-9.0295 82.2317Z";

  auto path1 = SVGPathParser::FromSVGString(pathString);
  ASSERT_TRUE(path1 != nullptr);

  // Parse second SVG path (a rectangle)
  auto path2 = SVGPathParser::FromSVGString(
      "M0.9196 28.102Q0 33.3382 0.7942 36.5272Q1.1585 37.9899 1.923 38.4949Q2.6874 38.9997 3.7621 "
      "38.5685Q4.5362 38.2578 5.6399 37.3623Q6.2871 36.837 7.6974 35.5417Q9.857 33.5582 10.9865 "
      "32.7335Q12.8436 31.3774 14.2638 31.1367Q17.8231 30.5334 20.0511 37.1224Q20.3787 38.0914 "
      "20.8593 38.3182Q21.5334 38.6362 22.0531 37.8143Q22.6626 36.8504 23.1356 34.0181Q23.6019 "
      "31.226 23.793 27.5435Q23.9999 23.5595 23.8171 19.6582Q23.3673 10.0598 20.8628 "
      "5.0886Q19.3508 2.0875 17.1624 1.1038Q14.7067 -0 11.5881 1.4946L11.4679 1.5522L11.3924 "
      "1.6619Q9.7061 4.111 7.6259 8.4628Q5.3932 13.1338 3.6739 18.02Q1.7103 23.6003 0.9196 "
      "28.102ZM2.4742 37.6604Q2.0347 37.3702 1.7646 36.2855Q1.0216 33.3023 1.9046 28.275Q2.681 "
      "23.8544 4.6172 18.352Q6.3186 13.5165 8.5282 8.894Q10.5272 4.7119 12.1402 2.3398Q17.1816 "
      "0.0044 19.9697 5.5385Q22.3784 10.3194 22.8182 19.705Q22.9987 23.557 22.7944 27.4916Q22.6061 "
      "31.1173 22.1493 33.8534Q21.7095 36.4866 21.2079 37.2798Q21.2018 37.2895 21.1961 "
      "37.2983Q21.1149 37.1466 20.9984 36.8021Q18.4972 29.4049 14.0967 30.1507Q12.4442 30.4308 "
      "10.3968 31.9258Q9.2219 32.7837 7.021 34.8052Q5.6332 36.0798 5.0098 36.5858Q4.0235 37.3861 "
      "3.3896 37.6404Q2.8013 37.8765 2.4742 37.6604Z");
  ASSERT_TRUE(path2 != nullptr);

  // Intersect path1 with path2
  path1->addPath(*path2, PathOp::Intersect);

  // Calculate position to center the result
  auto resultBounds = path1->getBounds();
  float offsetX = (400 - resultBounds.width()) / 2 - resultBounds.left;
  float offsetY = (400 - resultBounds.height()) / 2 - resultBounds.top;

  // Draw the result
  Paint paint;
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  paint.setAntiAlias(true);

  canvas->translate(offsetX, offsetY);
  canvas->drawPath(*path1, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "PathTest/TwoPathsIntersect"));
}

}  // namespace tgfx
