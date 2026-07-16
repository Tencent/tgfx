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

TGFX_TEST(RRectUtilsTest, ConservativeIntersect) {
  // Case 1: Bounding rects do not overlap, producing an explicit empty rect-typed RRect.
  {
    const auto a = RRect::MakeRectXY(Rect::MakeXYWH(0, 0, 50, 50), 10, 10);
    const auto b = RRect::MakeRectXY(Rect::MakeXYWH(100, 100, 50, 50), 10, 10);
    const auto result = RRectUtils::ConservativeIntersect(a, b);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->rect().isEmpty());
  }

  // Case 2: Two identical RRects, result equals either input.
  {
    const auto a = RRect::MakeRectXY(Rect::MakeWH(100, 80), 10, 12);
    const auto result = RRectUtils::ConservativeIntersect(a, a);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rect(), a.rect());
    EXPECT_EQ(result->radii(), a.radii());
  }

  // Case 3: A rect (zero radii) and a rounded rect share the same corner anchor; the side with
  // the larger radii dominates, so the rounded rect wins.
  {
    RRect rectAsRRect = {};
    rectAsRRect.setRectXY(Rect::MakeLTRB(0, 0, 100, 100), 0, 0);
    const auto rRect = RRect::MakeRectXY(Rect::MakeLTRB(0, 0, 100, 100), 10, 10);
    const auto result = RRectUtils::ConservativeIntersect(rectAsRRect, rRect);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rect(), rRect.rect());
    EXPECT_EQ(result->radii(), rRect.radii());
  }

  // Case 4: Offset RRects where a's BR and b's TL coincide with intersection corners; the result
  // keeps two anchored rounded corners and collapses the straight-edge corners to (0, 0).
  {
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

  // Case 5: Two RRects share the same anchor corner with identical radii; the inside check passes.
  {
    RRect a = {};
    a.setRectRadii(Rect::MakeLTRB(0, 0, 100, 100), {{{15, 15}, {0, 0}, {0, 0}, {0, 0}}});
    RRect b = {};
    b.setRectRadii(Rect::MakeLTRB(0, 0, 200, 200), {{{15, 15}, {0, 0}, {0, 0}, {0, 0}}});
    const auto result = RRectUtils::ConservativeIntersect(a, b);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rect(), a.rect());
  }

  // Case 6: Same anchor corner with crossing X/Y radii where neither dominates, rejected.
  {
    RRect a = {};
    a.setRectRadii(Rect::MakeWH(100, 100), {{{20, 5}, {0, 0}, {0, 0}, {0, 0}}});
    RRect b = {};
    b.setRectRadii(Rect::MakeWH(100, 100), {{{5, 20}, {0, 0}, {0, 0}, {0, 0}}});
    EXPECT_FALSE(RRectUtils::ConservativeIntersect(a, b).has_value());
  }

  // Case 7: After intersection the sum of adjacent radii on the top edge exceeds the side length,
  // rejected.
  {
    RRect a = {};
    a.setRectRadii(Rect::MakeWH(100, 100), {{{60, 20}, {0, 0}, {0, 0}, {0, 0}}});
    RRect b = {};
    b.setRectRadii(Rect::MakeWH(100, 100), {{{0, 0}, {60, 20}, {0, 0}, {0, 0}}});
    EXPECT_FALSE(RRectUtils::ConservativeIntersect(a, b).has_value());
  }

  // Case 8: Two ovals with the same bounding rect.
  {
    const auto a = RRect::MakeOval(Rect::MakeWH(100, 80));
    const auto result = RRectUtils::ConservativeIntersect(a, a);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type(), RRect::Type::Oval);
  }

  // Case 9: b's TL corner lies inside a's TL ellipse, so containment passes and b is the result.
  {
    const auto a = RRect::MakeRectXY(Rect::MakeLTRB(0, 0, 100, 100), 30, 30);
    const auto b = RRect::MakeRectXY(Rect::MakeLTRB(10, 10, 90, 90), 5, 5);
    const auto result = RRectUtils::ConservativeIntersect(a, b);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rect(), b.rect());
  }

  // Case 10: b's TL corner lies outside a's large rounded boundary, so containment fails, rejected.
  {
    const auto a = RRect::MakeRectXY(Rect::MakeLTRB(0, 0, 100, 100), 50, 50);
    const auto b = RRect::MakeRectXY(Rect::MakeLTRB(2, 2, 90, 90), 0, 0);
    EXPECT_FALSE(RRectUtils::ConservativeIntersect(a, b).has_value());
  }
}

TGFX_TEST(RRectUtilsTest, InnerBounds) {
  // Case 1: A plain rect has no corners to avoid, so the inner bounds equal the rect.
  {
    const auto rr = RRect::MakeRectXY(Rect::MakeWH(100, 80), 0, 0);
    EXPECT_EQ(RRectUtils::InnerBounds(rr), Rect::MakeWH(100, 80));
  }

  // Case 2: Thin horizontal radii and tall vertical radii; the largest inscribed rect keeps the
  // full height and insets only the left/right edges.
  {
    const auto rr = RRect::MakeRectXY(Rect::MakeWH(100, 100), 5, 40);
    EXPECT_EQ(RRectUtils::InnerBounds(rr), Rect::MakeLTRB(5, 0, 95, 100));
  }

  // Case 3: Wide horizontal radii and thin vertical radii; the largest inscribed rect keeps the
  // full width and insets only the top/bottom edges.
  {
    const auto rr = RRect::MakeRectXY(Rect::MakeWH(100, 100), 40, 5);
    EXPECT_EQ(RRectUtils::InnerBounds(rr), Rect::MakeLTRB(0, 5, 100, 95));
  }

  // Case 4: Uniform radii on a wide rect; the diagonal candidate wins, insetting all four edges
  // by a fraction of the corner shift.
  {
    const auto rr = RRect::MakeRectXY(Rect::MakeWH(200, 100), 20, 20);
    const auto inner = RRectUtils::InnerBounds(rr);
    const float inset = 5.8580644f;
    EXPECT_NEAR(inner.left, inset, 1e-3f);
    EXPECT_NEAR(inner.top, inset, 1e-3f);
    EXPECT_NEAR(inner.right, 200.f - inset, 1e-3f);
    EXPECT_NEAR(inner.bottom, 100.f - inset, 1e-3f);
  }

  // Case 5: An oval falls into the diagonal branch since both full insets collapse the rect.
  {
    const auto rr = RRect::MakeOval(Rect::MakeWH(100, 80));
    const auto inner = RRectUtils::InnerBounds(rr);
    EXPECT_NEAR(inner.left, 14.645161f, 1e-3f);
    EXPECT_NEAR(inner.top, 11.716129f, 1e-3f);
    EXPECT_NEAR(inner.right, 100.f - 14.645161f, 1e-3f);
    EXPECT_NEAR(inner.bottom, 80.f - 11.716129f, 1e-3f);
  }
}

TGFX_TEST(RRectUtilsTest, TryAxisAlignedTransform) {
  // Case 1: Identity matrix returns the source unchanged.
  {
    auto rRect =
        RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 10}, {10, 10}, {5, 5}, {15, 15}}});
    auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::I());
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->rect(), rRect.rect());
    EXPECT_EQ(out->radii(), rRect.radii());
    EXPECT_EQ(out->type(), rRect.type());
  }

  // Case 2: Pure translation shifts the rect and keeps radii unchanged.
  {
    auto rRect = RRect::MakeRectXY(Rect::MakeXYWH(10, 20, 100, 80), 8, 12);
    auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::MakeTrans(5, -3));
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->rect(), Rect::MakeXYWH(15, 17, 100, 80));
    EXPECT_EQ(out->radii()[0], (Point{8, 12}));
    EXPECT_EQ(out->type(), RRect::Type::Simple);
  }

  // Case 3: Positive scale stretches the rect and radii with no corner swap.
  {
    auto rRect =
        RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 10}, {10, 10}, {5, 5}, {15, 15}}});
    auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::MakeScale(2, 0.5f));
    ASSERT_TRUE(out.has_value());
    EXPECT_FLOAT_EQ(out->rect().width(), 200.f);
    EXPECT_FLOAT_EQ(out->rect().height(), 40.f);
    EXPECT_FLOAT_EQ(out->radii()[0].x, 40.f);
    EXPECT_FLOAT_EQ(out->radii()[0].y, 5.f);
  }

  // Case 4: flipX swaps the left/right corner pairs.
  {
    auto rRect =
        RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 10}, {30, 15}, {5, 5}, {15, 15}}});
    auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::MakeScale(-1, 1));
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->rect(), Rect::MakeXYWH(-100, 0, 100, 80));
    EXPECT_EQ(out->radii()[0], (Point{30, 15}));  // was TR
    EXPECT_EQ(out->radii()[1], (Point{20, 10}));  // was TL
    EXPECT_EQ(out->radii()[2], (Point{15, 15}));  // was BL
    EXPECT_EQ(out->radii()[3], (Point{5, 5}));    // was BR
  }

  // Case 5: flipY swaps the top/bottom corner pairs.
  {
    auto rRect =
        RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 10}, {30, 15}, {5, 5}, {15, 15}}});
    auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::MakeScale(1, -1));
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->radii()[0], (Point{15, 15}));  // was BL
    EXPECT_EQ(out->radii()[1], (Point{5, 5}));    // was BR
    EXPECT_EQ(out->radii()[2], (Point{30, 15}));  // was TR
    EXPECT_EQ(out->radii()[3], (Point{20, 10}));  // was TL
  }

  // Case 6: 90 degrees clockwise swaps width/height and shifts corners with x/y swapped.
  {
    auto rRect =
        RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 10}, {30, 15}, {5, 5}, {15, 15}}});
    Matrix m = {};
    m.setRotate(90);
    auto out = RRectUtils::TryAxisAlignedTransform(rRect, m);
    ASSERT_TRUE(out.has_value());
    EXPECT_FLOAT_EQ(out->rect().width(), 80.f);
    EXPECT_FLOAT_EQ(out->rect().height(), 100.f);
    EXPECT_FLOAT_EQ(out->radii()[0].x, 15.f);  // old BL.y
    EXPECT_FLOAT_EQ(out->radii()[0].y, 15.f);  // old BL.x
  }

  // Case 7: 270 degrees clockwise (= 90 counter-clockwise).
  {
    auto rRect =
        RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 10}, {30, 15}, {5, 5}, {15, 15}}});
    Matrix m = {};
    m.setRotate(270);
    auto out = RRectUtils::TryAxisAlignedTransform(rRect, m);
    ASSERT_TRUE(out.has_value());
    EXPECT_FLOAT_EQ(out->rect().width(), 80.f);
    EXPECT_FLOAT_EQ(out->rect().height(), 100.f);
    EXPECT_FLOAT_EQ(out->radii()[0].x, 15.f);  // old TR.y
    EXPECT_FLOAT_EQ(out->radii()[0].y, 30.f);  // old TR.x
  }

  // Case 8: scale composed with a 90 degree rotation produces a valid axis-aligned RRect.
  {
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

  // Case 9: Shear breaks axis alignment, rejected.
  {
    auto rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 8, 12);
    Matrix m = Matrix::I();
    m.setSkew(0.5f, 0);
    EXPECT_FALSE(RRectUtils::TryAxisAlignedTransform(rRect, m).has_value());
  }

  // Case 10: Perspective breaks axis alignment, rejected.
  {
    auto rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 8, 12);
    Matrix m = Matrix::I();
    m.setAll(1, 0, 0, 0, 1, 0, 0.001f, 0, 1);
    EXPECT_FALSE(RRectUtils::TryAxisAlignedTransform(rRect, m).has_value());
  }

  // Case 11: Zero scale collapses the rect, rejected.
  {
    auto rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 8, 12);
    auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::MakeScale(0, 1));
    EXPECT_FALSE(out.has_value());
  }

  // Case 12: An oval keeps its type and scales its half-axes.
  {
    auto rRect = RRect::MakeOval(Rect::MakeWH(100, 80));
    EXPECT_EQ(rRect.type(), RRect::Type::Oval);
    auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::MakeScale(2, 1));
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->type(), RRect::Type::Oval);
    EXPECT_FLOAT_EQ(out->radii()[0].x, 100.f);
    EXPECT_FLOAT_EQ(out->radii()[0].y, 40.f);
  }

  // Case 13: A plain rect keeps its type under scale.
  {
    auto rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 0, 0);
    EXPECT_EQ(rRect.type(), RRect::Type::Rect);
    auto out = RRectUtils::TryAxisAlignedTransform(rRect, Matrix::MakeScale(2, 0.5f));
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->type(), RRect::Type::Rect);
    EXPECT_FLOAT_EQ(out->rect().width(), 200.f);
    EXPECT_FLOAT_EQ(out->rect().height(), 40.f);
  }
}

}  // namespace tgfx
