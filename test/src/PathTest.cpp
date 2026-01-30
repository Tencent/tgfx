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

#include <cmath>
#include <cstddef>
#include "base/TGFXTest.h"
#include "core/PathRasterizer.h"
#include "core/PathTriangulator.h"
#include "core/ShapeRasterizer.h"
#include "gtest/gtest.h"
#include "tgfx/core/CurveConverter.h"
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

  path.decompose(
      [&](PathVerb verb, const Point pts[4], float weight, void*) {
        verbs.push_back(verb);
        weights.push_back(weight);
        switch (verb) {
          case PathVerb::Move:
            points.push_back(pts[0]);
            break;
          case PathVerb::Line:
            points.push_back(pts[1]);
            break;
          case PathVerb::Quad:
            points.push_back(pts[1]);
            points.push_back(pts[2]);
            break;
          case PathVerb::Conic:
            points.push_back(pts[1]);
            points.push_back(pts[2]);
            break;
          case PathVerb::Cubic:
            points.push_back(pts[1]);
            points.push_back(pts[2]);
            points.push_back(pts[3]);
            break;
          case PathVerb::Close:
            break;
        }
      },
      nullptr);

  ASSERT_EQ(verbs.size(), 2u);
  ASSERT_EQ(verbs[0], PathVerb::Move);
  ASSERT_EQ(verbs[1], PathVerb::Conic);

  ASSERT_EQ(weights[0], 0.0f);
  ASSERT_NEAR(weights[1], 0.707107f, 0.0001f);

  ASSERT_EQ(points.size(), 3u);
  ASSERT_EQ(points[0].x, 0.0f);
  ASSERT_EQ(points[0].y, 100.0f);
  ASSERT_EQ(points[1].x, 100.0f);
  ASSERT_EQ(points[1].y, 0.0f);
  ASSERT_EQ(points[2].x, 200.0f);
  ASSERT_EQ(points[2].y, 100.0f);
}

TGFX_TEST(PathTest, ConicToQuads) {
  Point p0 = {0, 100};
  Point p1 = {100, 0};
  Point p2 = {200, 100};
  float weight = 0.707107f;

  auto quads = CurveConverter::ConicToQuads(p0, p1, p2, weight);
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

TGFX_TEST(PathTest, ConicToCubics) {
  // Test 1: Normal conic (non-90-degree arc)
  {
    Point p0 = {0, 100};
    Point p1 = {100, 0};
    Point p2 = {200, 100};
    float weight = 0.5f;

    auto cubics = CurveConverter::ConicToCubics(p0, p1, p2, weight);
    size_t numCubics = (cubics.size() - 1) / 3;

    ASSERT_EQ(numCubics, 2u);
    ASSERT_EQ(cubics[0].x, p0.x);
    ASSERT_EQ(cubics[0].y, p0.y);
    ASSERT_EQ(cubics[6].x, p2.x);
    ASSERT_EQ(cubics[6].y, p2.y);
  }

  // Test 2: 90-degree circular arc (uses optimal kappa approximation)
  {
    // A 90-degree arc from a RoundRect corner: center at (100, 100), radius 100
    // Arc from (0, 100) to (100, 0) with control point at (0, 0)
    Point p0 = {0, 100};
    Point p1 = {0, 0};
    Point p2 = {100, 0};
    float weight = 0.707106781186548f;

    auto cubics = CurveConverter::ConicToCubics(p0, p1, p2, weight, 0);
    size_t numCubics = (cubics.size() - 1) / 3;

    ASSERT_EQ(numCubics, 1u);
    ASSERT_EQ(cubics[0].x, p0.x);
    ASSERT_EQ(cubics[0].y, p0.y);
    ASSERT_EQ(cubics[3].x, p2.x);
    ASSERT_EQ(cubics[3].y, p2.y);

    // Verify control points use kappa = 0.552284749830794
    // cubic[1] = p0 + kappa * (p1 - p0) = (0, 100) + 0.5523 * (0-0, 0-100) = (0, 44.77)
    // cubic[2] = p2 + kappa * (p1 - p2) = (100, 0) + 0.5523 * (0-100, 0-0) = (44.77, 0)
    ASSERT_NEAR(cubics[1].x, 0.0f, 0.001f);
    ASSERT_NEAR(cubics[1].y, 44.7715f, 0.001f);
    ASSERT_NEAR(cubics[2].x, 44.7715f, 0.001f);
    ASSERT_NEAR(cubics[2].y, 0.0f, 0.001f);
  }

  // Test 3: 90-degree arc with pow2 > 0 (still uses optimal kappa, pow2 is ignored)
  {
    Point p0 = {0, 100};
    Point p1 = {0, 0};
    Point p2 = {100, 0};
    float weight = 0.707106781186548f;

    auto cubics = CurveConverter::ConicToCubics(p0, p1, p2, weight, 1);
    size_t numCubics = (cubics.size() - 1) / 3;

    ASSERT_EQ(numCubics, 1u);
    ASSERT_NEAR(cubics[1].x, 0.0f, 0.001f);
    ASSERT_NEAR(cubics[1].y, 44.7715f, 0.001f);
    ASSERT_NEAR(cubics[2].x, 44.7715f, 0.001f);
    ASSERT_NEAR(cubics[2].y, 0.0f, 0.001f);
  }
}

}  // namespace tgfx
