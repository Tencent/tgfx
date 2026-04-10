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

#include "gtest/gtest.h"
#include "tgfx/core/RRect.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(RRectTest, SetRectXY) {
  RRect rRect = {};
  rRect.setRectXY(Rect::MakeWH(100, 80), 10, 15);
  EXPECT_EQ(rRect.type, RRect::Type::Simple);
  EXPECT_EQ(rRect.radii[0], (Point{10, 15}));
  EXPECT_EQ(rRect.radii[1], (Point{10, 15}));
  EXPECT_EQ(rRect.radii[2], (Point{10, 15}));
  EXPECT_EQ(rRect.radii[3], (Point{10, 15}));

  // Zero radii → Rect type.
  rRect.setRectXY(Rect::MakeWH(100, 80), 0, 0);
  EXPECT_EQ(rRect.type, RRect::Type::Rect);
  EXPECT_TRUE(rRect.isRect());

  // Radii filling the rect → Oval type.
  rRect.setRectXY(Rect::MakeWH(100, 80), 50, 40);
  EXPECT_EQ(rRect.type, RRect::Type::Oval);
  EXPECT_TRUE(rRect.isOval());

  // Radii exceeding rect size → clamped proportionally.
  rRect.setRectXY(Rect::MakeWH(100, 80), 200, 200);
  // scale = min(100/400, 80/400) = 0.2, so radii = (40, 40), type = Simple (40 < 50).
  EXPECT_EQ(rRect.type, RRect::Type::Simple);
  EXPECT_FLOAT_EQ(rRect.radii[0].x, 40.f);
  EXPECT_FLOAT_EQ(rRect.radii[0].y, 40.f);
}

TGFX_TEST(RRectTest, SetOval) {
  RRect rRect = {};
  rRect.setOval(Rect::MakeWH(120, 80));
  EXPECT_EQ(rRect.type, RRect::Type::Oval);
  EXPECT_TRUE(rRect.isOval());
  EXPECT_EQ(rRect.radii[0], (Point{60, 40}));
  EXPECT_EQ(rRect.radii[2], (Point{60, 40}));
}

TGFX_TEST(RRectTest, SetRectRadii_Simple) {
  // All corners the same → Simple type.
  RRect rRect = {};
  auto p = Point{10, 15};
  rRect.setRectRadii(Rect::MakeWH(100, 80), {p, p, p, p});
  EXPECT_EQ(rRect.type, RRect::Type::Simple);
  EXPECT_TRUE(rRect.isSimple());
}

TGFX_TEST(RRectTest, SetRectRadii_Complex) {
  // Different corners → Complex type.
  RRect rRect = {};
  rRect.setRectRadii(Rect::MakeWH(100, 80), {{{20, 20}, {10, 10}, {0, 0}, {30, 30}}});
  EXPECT_EQ(rRect.type, RRect::Type::Complex);
  EXPECT_TRUE(rRect.isComplex());
  EXPECT_EQ(rRect.radii[0], (Point{20, 20}));
  EXPECT_EQ(rRect.radii[1], (Point{10, 10}));
  EXPECT_EQ(rRect.radii[2], (Point{0, 0}));
  EXPECT_EQ(rRect.radii[3], (Point{30, 30}));
}

TGFX_TEST(RRectTest, SetRectRadii_AllZero) {
  // All zero → Rect type.
  RRect rRect = {};
  rRect.setRectRadii(Rect::MakeWH(100, 80), {{{0, 0}, {0, 0}, {0, 0}, {0, 0}}});
  EXPECT_EQ(rRect.type, RRect::Type::Rect);
  EXPECT_TRUE(rRect.isRect());
}

TGFX_TEST(RRectTest, RadiiScaling) {
  // W3C CSS scaling: adjacent corner radii that exceed the edge length should be scaled down.
  // Top edge: TL.x + TR.x = 60 + 60 = 120 > 100 → scale factor = 100/120 ≈ 0.833
  RRect rRect = {};
  rRect.setRectRadii(Rect::MakeWH(100, 100), {{{60, 10}, {60, 10}, {10, 10}, {10, 10}}});
  // All radii should be scaled by the same factor.
  EXPECT_LE(rRect.radii[0].x + rRect.radii[1].x, 100.f + 1e-5f);
  // The original ratio between corners should be preserved.
  EXPECT_NEAR(rRect.radii[0].x, rRect.radii[1].x, 1e-5f);
}

TGFX_TEST(RRectTest, NegativeRadii) {
  // Negative radii should be clamped to zero.
  RRect rRect = {};
  rRect.setRectRadii(Rect::MakeWH(100, 80), {{{-10, -5}, {10, 10}, {10, 10}, {10, 10}}});
  EXPECT_EQ(rRect.radii[0], (Point{0, 0}));
  EXPECT_EQ(rRect.radii[1], (Point{10, 10}));
}

TGFX_TEST(RRectTest, Scale) {
  RRect rRect = {};
  rRect.setRectRadii(Rect::MakeWH(100, 80), {{{20, 10}, {10, 10}, {5, 5}, {15, 15}}});
  rRect.scale(2.0f, 0.5f);
  EXPECT_FLOAT_EQ(rRect.rect.width(), 200.f);
  EXPECT_FLOAT_EQ(rRect.rect.height(), 40.f);
  EXPECT_FLOAT_EQ(rRect.radii[0].x, 40.f);
  EXPECT_FLOAT_EQ(rRect.radii[0].y, 5.f);
  EXPECT_FLOAT_EQ(rRect.radii[2].x, 10.f);
  EXPECT_FLOAT_EQ(rRect.radii[2].y, 2.5f);
}

TGFX_TEST(RRectTest, QueryMethods) {
  RRect rRect = {};
  // Default → Rect
  EXPECT_TRUE(rRect.isRect());
  EXPECT_FALSE(rRect.isOval());
  EXPECT_FALSE(rRect.isSimple());
  EXPECT_FALSE(rRect.isComplex());

  rRect.setRectXY(Rect::MakeWH(100, 80), 10, 10);
  EXPECT_FALSE(rRect.isRect());
  EXPECT_FALSE(rRect.isOval());
  EXPECT_TRUE(rRect.isSimple());
  EXPECT_FALSE(rRect.isComplex());

  rRect.setOval(Rect::MakeWH(100, 80));
  EXPECT_FALSE(rRect.isRect());
  EXPECT_TRUE(rRect.isOval());
  EXPECT_FALSE(rRect.isSimple());
  EXPECT_FALSE(rRect.isComplex());

  rRect.setRectRadii(Rect::MakeWH(100, 80), {{{20, 20}, {10, 10}, {5, 5}, {30, 30}}});
  EXPECT_FALSE(rRect.isRect());
  EXPECT_FALSE(rRect.isOval());
  EXPECT_FALSE(rRect.isSimple());
  EXPECT_TRUE(rRect.isComplex());
}

}  // namespace tgfx
