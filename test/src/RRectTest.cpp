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
  EXPECT_EQ(rRect.type(), RRect::Type::Simple);
  EXPECT_EQ(rRect.radii()[0], (Point{10, 15}));
  EXPECT_EQ(rRect.radii()[1], (Point{10, 15}));
  EXPECT_EQ(rRect.radii()[2], (Point{10, 15}));
  EXPECT_EQ(rRect.radii()[3], (Point{10, 15}));

  // Zero radii → Rect type.
  rRect.setRectXY(Rect::MakeWH(100, 80), 0, 0);
  EXPECT_EQ(rRect.type(), RRect::Type::Rect);

  // Radii filling the rect → Oval type.
  rRect.setRectXY(Rect::MakeWH(100, 80), 50, 40);
  EXPECT_EQ(rRect.type(), RRect::Type::Oval);

  // Radii exceeding rect size → clamped proportionally. The clamped x radius (40) is less than
  // halfWidth (50), so the result is still Simple rather than Oval.
  rRect.setRectXY(Rect::MakeWH(100, 80), 200, 200);
  EXPECT_EQ(rRect.type(), RRect::Type::Simple);
  EXPECT_FLOAT_EQ(rRect.radii()[0].x, 40.f);
  EXPECT_FLOAT_EQ(rRect.radii()[0].y, 40.f);
}

TGFX_TEST(RRectTest, SetOval) {
  auto rRect = RRect::MakeOval(Rect::MakeWH(120, 80));
  EXPECT_EQ(rRect.type(), RRect::Type::Oval);
  EXPECT_EQ(rRect.radii()[0], (Point{60, 40}));
  EXPECT_EQ(rRect.radii()[2], (Point{60, 40}));
}

TGFX_TEST(RRectTest, SetRectRadii_Complex) {
  // Different corners → Complex type.
  auto rRect =
      RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 20}, {10, 10}, {0, 0}, {30, 30}}});
  EXPECT_EQ(rRect.type(), RRect::Type::Complex);
  EXPECT_EQ(rRect.radii()[0], (Point{20, 20}));
  EXPECT_EQ(rRect.radii()[1], (Point{10, 10}));
  EXPECT_EQ(rRect.radii()[2], (Point{0, 0}));
  EXPECT_EQ(rRect.radii()[3], (Point{30, 30}));
}

TGFX_TEST(RRectTest, RadiiScaling) {
  // Adjacent corner radii that exceed the edge length should be scaled down proportionally.
  // Top edge: TL.x + TR.x = 60 + 60 = 120 > 100 → scale factor = 100/120 ≈ 0.833
  auto rRect =
      RRect::MakeRectRadii(Rect::MakeWH(100, 100), {{{60, 10}, {60, 10}, {10, 10}, {10, 10}}});
  // All radii should be scaled by the same factor.
  EXPECT_LE(rRect.radii()[0].x + rRect.radii()[1].x, 100.f + 1e-5f);
  // The original ratio between corners should be preserved.
  EXPECT_NEAR(rRect.radii()[0].x, rRect.radii()[1].x, 1e-5f);
  // ScaleRadii applies the single minimum scale factor (5/6) to every corner uniformly,
  // preserving the overall shape proportions.
  constexpr float kScale = 100.f / 120.f;
  EXPECT_NEAR(rRect.radii()[0].x, 60.f * kScale, 1e-5f);
  EXPECT_NEAR(rRect.radii()[0].y, 10.f * kScale, 1e-5f);
  EXPECT_NEAR(rRect.radii()[1].x, 60.f * kScale, 1e-5f);
  EXPECT_NEAR(rRect.radii()[1].y, 10.f * kScale, 1e-5f);
  EXPECT_NEAR(rRect.radii()[2].x, 10.f * kScale, 1e-5f);
  EXPECT_NEAR(rRect.radii()[2].y, 10.f * kScale, 1e-5f);
  EXPECT_NEAR(rRect.radii()[3].x, 10.f * kScale, 1e-5f);
  EXPECT_NEAR(rRect.radii()[3].y, 10.f * kScale, 1e-5f);
}

TGFX_TEST(RRectTest, NegativeRadii) {
  // Negative radii should be clamped to zero.
  auto rRect =
      RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{-10, -5}, {10, 10}, {10, 10}, {10, 10}}});
  EXPECT_EQ(rRect.radii()[0], (Point{0, 0}));
  EXPECT_EQ(rRect.radii()[1], (Point{10, 10}));
}

TGFX_TEST(RRectTest, Scale) {
  auto rRect =
      RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 10}, {10, 10}, {5, 5}, {15, 15}}});
  rRect.scale(2.0f, 0.5f);
  EXPECT_FLOAT_EQ(rRect.rect().width(), 200.f);
  EXPECT_FLOAT_EQ(rRect.rect().height(), 40.f);
  EXPECT_FLOAT_EQ(rRect.radii()[0].x, 40.f);
  EXPECT_FLOAT_EQ(rRect.radii()[0].y, 5.f);
  EXPECT_FLOAT_EQ(rRect.radii()[2].x, 10.f);
  EXPECT_FLOAT_EQ(rRect.radii()[2].y, 2.5f);
}

}  // namespace tgfx
