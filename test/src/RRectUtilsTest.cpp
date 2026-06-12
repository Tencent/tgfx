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

#include "core/RRectUtils.h"
#include "gtest/gtest.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(RRectUtilsTest, DisjointBoundsProducesEmpty) {
  // Bounding rects do not overlap: result is an explicit empty rect-typed RRect.
  const auto a = RRect::MakeRectXY(Rect::MakeXYWH(0, 0, 50, 50), 10, 10);
  const auto b = RRect::MakeRectXY(Rect::MakeXYWH(100, 100, 50, 50), 10, 10);
  const auto result = RRectUtils::ConservativeIntersect(a, b);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->rect().isEmpty());
}

TGFX_TEST(RRectUtilsTest, IdenticalRRectReturnsSelf) {
  // Two identical RRects: result equals either input.
  const auto a = RRect::MakeRectXY(Rect::MakeWH(100, 80), 10, 12);
  const auto result = RRectUtils::ConservativeIntersect(a, a);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->rect(), a.rect());
  EXPECT_EQ(result->radii(), a.radii());
}

TGFX_TEST(RRectUtilsTest, NestedRRectInsideLarger) {
  // The smaller RRect lies completely inside the larger and shares no corners. The intersection
  // is the smaller one.
  const auto outer = RRect::MakeRectXY(Rect::MakeLTRB(0, 0, 200, 200), 30, 30);
  const auto inner = RRect::MakeRectXY(Rect::MakeLTRB(50, 50, 150, 150), 10, 10);
  const auto result = RRectUtils::ConservativeIntersect(outer, inner);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->rect(), inner.rect());
  EXPECT_EQ(result->radii(), inner.radii());
}

TGFX_TEST(RRectUtilsTest, RectIntersectRRectPicksRectAtSharedCorner) {
  // A rect (zero radii) and a rounded rect sharing the same corner: when test == aCorner ==
  // bCorner, the side with the larger radii wins. The rect has all-zero radii, so the RRect
  // (positive radii) is the dominant side.
  RRect rectAsRRect = {};
  rectAsRRect.setRectXY(Rect::MakeLTRB(0, 0, 100, 100), 0, 0);
  const auto rRect = RRect::MakeRectXY(Rect::MakeLTRB(0, 0, 100, 100), 10, 10);
  const auto result = RRectUtils::ConservativeIntersect(rectAsRRect, rRect);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->rect(), rRect.rect());
  EXPECT_EQ(result->radii(), rRect.radii());
}

TGFX_TEST(RRectUtilsTest, NonSharedCornerUsesCornerContainment) {
  // Each corner of the intersection rect is classified independently:
  //   - When it coincides with an input's anchor, that input's radii are inherited
  //   - Otherwise it's "formed by two straight edges" and radii collapse to (0, 0), provided
  //     both inputs contain the point.
  // Here a's BR (100, 100) and b's TL (30, 30) both coincide with intersection corners, so the
  // result keeps two rounded corners and two sharp corners.
  const auto a = RRect::MakeRectXY(Rect::MakeLTRB(0, 0, 100, 100), 10, 10);
  const auto b = RRect::MakeRectXY(Rect::MakeLTRB(30, 30, 200, 200), 10, 10);
  const auto result = RRectUtils::ConservativeIntersect(a, b);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->rect(), Rect::MakeLTRB(30, 30, 100, 100));
  EXPECT_EQ(result->radii()[0], (Point{10, 10}));  // TL anchored by b
  EXPECT_EQ(result->radii()[1], (Point{0, 0}));    // TR straight-edge
  EXPECT_EQ(result->radii()[2], (Point{10, 10}));  // BR anchored by a
  EXPECT_EQ(result->radii()[3], (Point{0, 0}));    // BL straight-edge
  EXPECT_EQ(result->type(), RRect::Type::Complex);
}

TGFX_TEST(RRectUtilsTest, SharedCornerWithSameRadiiInside) {
  // Two RRects share the same anchor corner with identical radii. The inside check passes
  // (a's anchor is "inside" b's relative to the corner direction).
  RRect a = {};
  a.setRectRadii(Rect::MakeLTRB(0, 0, 100, 100), {{{15, 15}, {0, 0}, {0, 0}, {0, 0}}});
  RRect b = {};
  // b's TL is at (0, 0) too, but b is wider; insideCorner test should pass since same radii.
  b.setRectRadii(Rect::MakeLTRB(0, 0, 200, 200), {{{15, 15}, {0, 0}, {0, 0}, {0, 0}}});
  const auto result = RRectUtils::ConservativeIntersect(a, b);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->rect(), a.rect());
}

TGFX_TEST(RRectUtilsTest, CrossingRadiiAtSharedCornerFails) {
  // Same anchor TL corner, different X/Y radii where neither dominates.
  RRect a = {};
  a.setRectRadii(Rect::MakeWH(100, 100), {{{20, 5}, {0, 0}, {0, 0}, {0, 0}}});
  RRect b = {};
  b.setRectRadii(Rect::MakeWH(100, 100), {{{5, 20}, {0, 0}, {0, 0}, {0, 0}}});
  EXPECT_FALSE(RRectUtils::ConservativeIntersect(a, b).has_value());
}

TGFX_TEST(RRectUtilsTest, AdjacentRadiiOverflowFails) {
  // After intersection, the sum of adjacent radii on the top edge would exceed the side length.
  RRect a = {};
  a.setRectRadii(Rect::MakeWH(100, 100), {{{60, 20}, {0, 0}, {0, 0}, {0, 0}}});
  RRect b = {};
  b.setRectRadii(Rect::MakeWH(100, 100), {{{0, 0}, {60, 20}, {0, 0}, {0, 0}}});
  EXPECT_FALSE(RRectUtils::ConservativeIntersect(a, b).has_value());
}

TGFX_TEST(RRectUtilsTest, OvalIntersectOvalSameRect) {
  // Two ovals with the same bounding rect: the intersection is the same oval.
  const auto a = RRect::MakeOval(Rect::MakeWH(100, 80));
  const auto result = RRectUtils::ConservativeIntersect(a, a);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->type(), RRect::Type::Oval);
}

TGFX_TEST(RRectUtilsTest, RRectInteriorContainsOtherCornerWithDifferentRadii) {
  // a's TL is at (0, 0) with radius (30, 30). b's TL is at (10, 10) with radius (5, 5).
  // b's TL corner (10, 10) lies inside a's TL ellipse, so the "test == bCorner" branch
  // hits CheckCornerContainment on a, which should pass.
  const auto a = RRect::MakeRectXY(Rect::MakeLTRB(0, 0, 100, 100), 30, 30);
  const auto b = RRect::MakeRectXY(Rect::MakeLTRB(10, 10, 90, 90), 5, 5);
  const auto result = RRectUtils::ConservativeIntersect(a, b);
  ASSERT_TRUE(result.has_value());
  // b is contained within a, so the result is b itself.
  EXPECT_EQ(result->rect(), b.rect());
}

TGFX_TEST(RRectUtilsTest, RRectCornerOutsideOtherEllipseFails) {
  // a has a large TL radius; b's TL corner is so close to a's TL that it lies *outside* a's
  // rounded boundary. CheckCornerContainment returns false → the merge is rejected.
  const auto a = RRect::MakeRectXY(Rect::MakeLTRB(0, 0, 100, 100), 50, 50);
  // Place b's TL just outside a's rounded ellipse: (2, 2) is within a's rect bounds but the
  // canonical point (2-50, 2-50) = (-48, -48) has normalized distance > 1.
  const auto b = RRect::MakeRectXY(Rect::MakeLTRB(2, 2, 90, 90), 0, 0);
  const auto result = RRectUtils::ConservativeIntersect(a, b);
  EXPECT_FALSE(result.has_value());
}

TGFX_TEST(RRectUtilsTest, TryAxisAlignedTransformIdentity) {
  auto rRect =
      RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 10}, {10, 10}, {5, 5}, {15, 15}}});
  auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::I());
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->rect(), rRect.rect());
  EXPECT_EQ(out->radii(), rRect.radii());
  EXPECT_EQ(out->type(), rRect.type());
}

TGFX_TEST(RRectUtilsTest, TryAxisAlignedTransformTranslate) {
  auto rRect = RRect::MakeRectXY(Rect::MakeXYWH(10, 20, 100, 80), 8, 12);
  auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::MakeTrans(5, -3));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->rect(), Rect::MakeXYWH(15, 17, 100, 80));
  EXPECT_EQ(out->radii()[0], (Point{8, 12}));
  EXPECT_EQ(out->type(), RRect::Type::Simple);
}

TGFX_TEST(RRectUtilsTest, TryAxisAlignedTransformScale) {
  auto rRect =
      RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 10}, {10, 10}, {5, 5}, {15, 15}}});
  auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::MakeScale(2, 0.5f));
  ASSERT_TRUE(out.has_value());
  EXPECT_FLOAT_EQ(out->rect().width(), 200.f);
  EXPECT_FLOAT_EQ(out->rect().height(), 40.f);
  // Top-left corner radii scale with the matrix, no swap on positive scale.
  EXPECT_FLOAT_EQ(out->radii()[0].x, 40.f);
  EXPECT_FLOAT_EQ(out->radii()[0].y, 5.f);
}

TGFX_TEST(RRectUtilsTest, TryAxisAlignedTransformMirrorX) {
  // flipX swaps left/right corner pairs.
  auto rRect =
      RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 10}, {30, 15}, {5, 5}, {15, 15}}});
  auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::MakeScale(-1, 1));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->rect(), Rect::MakeXYWH(-100, 0, 100, 80));
  // After flipX: TL <-> TR, BR <-> BL.
  EXPECT_EQ(out->radii()[0], (Point{30, 15}));  // was TR
  EXPECT_EQ(out->radii()[1], (Point{20, 10}));  // was TL
  EXPECT_EQ(out->radii()[2], (Point{15, 15}));  // was BL
  EXPECT_EQ(out->radii()[3], (Point{5, 5}));    // was BR
}

TGFX_TEST(RRectUtilsTest, TryAxisAlignedTransformMirrorY) {
  auto rRect =
      RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 10}, {30, 15}, {5, 5}, {15, 15}}});
  auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::MakeScale(1, -1));
  ASSERT_TRUE(out.has_value());
  // After flipY: TL <-> BL, TR <-> BR.
  EXPECT_EQ(out->radii()[0], (Point{15, 15}));  // was BL
  EXPECT_EQ(out->radii()[1], (Point{5, 5}));    // was BR
  EXPECT_EQ(out->radii()[2], (Point{30, 15}));  // was TR
  EXPECT_EQ(out->radii()[3], (Point{20, 10}));  // was TL
}

TGFX_TEST(RRectUtilsTest, TryAxisAlignedTransformRotate90) {
  // 90 degrees clockwise: width <-> height, corner shift, x/y swap.
  auto rRect =
      RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 10}, {30, 15}, {5, 5}, {15, 15}}});
  Matrix m = {};
  m.setRotate(90);
  auto out = RRectUtils::TryAxisAlignedTransform(rRect, m);
  ASSERT_TRUE(out.has_value());
  EXPECT_FLOAT_EQ(out->rect().width(), 80.f);
  EXPECT_FLOAT_EQ(out->rect().height(), 100.f);
  // Top-left of new rect comes from bottom-left of old rect (clockwise rotation),
  // with x/y swapped.
  EXPECT_FLOAT_EQ(out->radii()[0].x, 15.f);  // old BL.y
  EXPECT_FLOAT_EQ(out->radii()[0].y, 15.f);  // old BL.x
}

TGFX_TEST(RRectUtilsTest, TryAxisAlignedTransformRotate270) {
  // 270 degrees clockwise (= 90 ccw).
  auto rRect =
      RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 10}, {30, 15}, {5, 5}, {15, 15}}});
  Matrix m = {};
  m.setRotate(270);
  auto out = RRectUtils::TryAxisAlignedTransform(rRect, m);
  ASSERT_TRUE(out.has_value());
  EXPECT_FLOAT_EQ(out->rect().width(), 80.f);
  EXPECT_FLOAT_EQ(out->rect().height(), 100.f);
  // Top-left of new rect comes from top-right of old rect (counter-clockwise rotation),
  // with x/y swapped.
  EXPECT_FLOAT_EQ(out->radii()[0].x, 15.f);  // old TR.y
  EXPECT_FLOAT_EQ(out->radii()[0].y, 30.f);  // old TR.x
}

TGFX_TEST(RRectUtilsTest, TryAxisAlignedTransformComposite) {
  // scale * rotate90 should produce a valid axis-aligned RRect.
  auto rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 8, 12);
  Matrix m = Matrix::MakeScale(2, 3);
  Matrix r = {};
  r.setRotate(90);
  m.preConcat(r);
  auto out = RRectUtils::TryAxisAlignedTransform(rRect, m);
  ASSERT_TRUE(out.has_value());
  EXPECT_FLOAT_EQ(out->rect().width(), 80.f * 2.f);
  EXPECT_FLOAT_EQ(out->rect().height(), 100.f * 3.f);
}

TGFX_TEST(RRectUtilsTest, TryAxisAlignedTransformShearFails) {
  auto rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 8, 12);
  Matrix m = Matrix::I();
  m.setSkew(0.5f, 0);
  EXPECT_FALSE(RRectUtils::TryAxisAlignedTransform(rRect, m).has_value());
}

TGFX_TEST(RRectUtilsTest, TryAxisAlignedTransformPerspectiveFails) {
  auto rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 8, 12);
  Matrix m = Matrix::I();
  m.setAll(1, 0, 0, 0, 1, 0, 0.001f, 0, 1);
  EXPECT_FALSE(RRectUtils::TryAxisAlignedTransform(rRect, m).has_value());
}

TGFX_TEST(RRectUtilsTest, TryAxisAlignedTransformExtremeScaleEmptyFails) {
  auto rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 8, 12);
  // Zero scale collapses the rect.
  auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::MakeScale(0, 1));
  EXPECT_FALSE(out.has_value());
}

TGFX_TEST(RRectUtilsTest, TryAxisAlignedTransformOval) {
  auto rRect = RRect::MakeOval(Rect::MakeWH(100, 80));
  EXPECT_EQ(rRect.type(), RRect::Type::Oval);
  auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::MakeScale(2, 1));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->type(), RRect::Type::Oval);
  EXPECT_FLOAT_EQ(out->radii()[0].x, 100.f);
  EXPECT_FLOAT_EQ(out->radii()[0].y, 40.f);
}

TGFX_TEST(RRectUtilsTest, TryAxisAlignedTransformRect) {
  auto rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 0, 0);
  EXPECT_EQ(rRect.type(), RRect::Type::Rect);
  auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::MakeScale(2, 0.5f));
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->type(), RRect::Type::Rect);
  EXPECT_FLOAT_EQ(out->rect().width(), 200.f);
  EXPECT_FLOAT_EQ(out->rect().height(), 40.f);
}

}  // namespace tgfx
