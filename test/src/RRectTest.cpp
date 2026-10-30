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

#include <cmath>
#include "core/SmoothCornerCoeffs.h"
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

  // Zero radii -> Rect type.
  rRect.setRectXY(Rect::MakeWH(100, 80), 0, 0);
  EXPECT_EQ(rRect.type(), RRect::Type::Rect);

  // Radii filling the rect -> Oval type.
  rRect.setRectXY(Rect::MakeWH(100, 80), 50, 40);
  EXPECT_EQ(rRect.type(), RRect::Type::Oval);

  // Radii exceeding rect size -> clamped proportionally. The clamped x radius (40) is less than
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
  // Different corners -> Complex type.
  auto rRect =
      RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 20}, {10, 10}, {5, 5}, {30, 30}}});
  EXPECT_EQ(rRect.type(), RRect::Type::Complex);
  EXPECT_EQ(rRect.radii()[0], (Point{20, 20}));
  EXPECT_EQ(rRect.radii()[1], (Point{10, 10}));
  EXPECT_EQ(rRect.radii()[2], (Point{5, 5}));
  EXPECT_EQ(rRect.radii()[3], (Point{30, 30}));
}

TGFX_TEST(RRectTest, RadiiScaling) {
  // Adjacent corner radii that exceed the edge length should be scaled down proportionally.
  // Top edge: TL.x + TR.x = 60 + 60 = 120 > 100, so scale factor = 100/120 ~= 0.833.
  auto rRect =
      RRect::MakeRectRadii(Rect::MakeWH(100, 100), {{{60, 10}, {60, 10}, {10, 10}, {10, 10}}});
  // All radii should be scaled by the same factor.
  EXPECT_LE(rRect.radii()[0].x + rRect.radii()[1].x, 100.f + 1e-5f);
  // The original ratio between corners should be preserved.
  EXPECT_NEAR(rRect.radii()[0].x, rRect.radii()[1].x, 1e-5f);
  // ScaleRadii applies the single minimum scale factor (5/6) to every corner uniformly,
  // preserving the overall shape proportions.
  constexpr float scale = 100.f / 120.f;
  EXPECT_NEAR(rRect.radii()[0].x, 60.f * scale, 1e-5f);
  EXPECT_NEAR(rRect.radii()[0].y, 10.f * scale, 1e-5f);
  EXPECT_NEAR(rRect.radii()[1].x, 60.f * scale, 1e-5f);
  EXPECT_NEAR(rRect.radii()[1].y, 10.f * scale, 1e-5f);
  EXPECT_NEAR(rRect.radii()[2].x, 10.f * scale, 1e-5f);
  EXPECT_NEAR(rRect.radii()[2].y, 10.f * scale, 1e-5f);
  EXPECT_NEAR(rRect.radii()[3].x, 10.f * scale, 1e-5f);
  EXPECT_NEAR(rRect.radii()[3].y, 10.f * scale, 1e-5f);
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

TGFX_TEST(RRectTest, Smoothness) {
  // Out-of-range smoothness is clamped to [0, 1].
  auto rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 10, 10, -0.5f);
  EXPECT_FLOAT_EQ(rRect.smoothness(), 0.f);
  rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 10, 10, 2.0f);
  EXPECT_FLOAT_EQ(rRect.smoothness(), 1.f);

  // Short edges reduce only the smoothness: each smoothed corner needs (1 + s) * 60 along an
  // edge, and a 200 edge fits s up to 200 / 120 - 1. The radii stay at 60.
  rRect = RRect::MakeRectXY(Rect::MakeWH(200, 200), 60, 60, 1.0f);
  EXPECT_EQ(rRect.type(), RRect::Type::Simple);
  EXPECT_NEAR(rRect.smoothness(), 200.f / 120.f - 1.f, 1e-6f);
  EXPECT_FLOAT_EQ(rRect.radii()[0].x, 60.f);

  // Once the smoothness hits 0 the radii take over the degradation: 120 + 120 overflows the 200
  // edge, so both shrink to 100 and the result is an Oval.
  rRect = RRect::MakeRectXY(Rect::MakeWH(200, 200), 120, 120, 0.8f);
  EXPECT_EQ(rRect.type(), RRect::Type::Oval);
  EXPECT_FLOAT_EQ(rRect.smoothness(), 0.f);
  EXPECT_FLOAT_EQ(rRect.radii()[0].x, 100.f);

  // With unequal corners the tightest edge wins. The left edge holds 30 + 20 = 50 of radii in 80
  // pixels, allowing s up to 80 / 50 - 1 = 0.6.
  rRect =
      RRect::MakeRectRadii(Rect::MakeWH(100, 80), {{{20, 20}, {10, 10}, {5, 5}, {30, 30}}}, 1.0f);
  EXPECT_NEAR(rRect.smoothness(), 0.6f, 1e-6f);

  // An oval fills every edge, leaving no room for smoothing.
  rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 50, 40, 0.6f);
  EXPECT_EQ(rRect.type(), RRect::Type::Oval);
  EXPECT_FLOAT_EQ(rRect.smoothness(), 0.f);

  // Zero radii produce a plain rect, and the smoothness is normalized to 0 as well.
  rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 0, 0, 0.6f);
  EXPECT_EQ(rRect.type(), RRect::Type::Rect);
  EXPECT_FLOAT_EQ(rRect.smoothness(), 0.f);
}

TGFX_TEST(RRectTest, SmoothnessScaleOffset) {
  auto rRect = RRect::MakeRectXY(Rect::MakeWH(200, 200), 60, 60, 0.5f);
  EXPECT_FLOAT_EQ(rRect.smoothness(), 0.5f);
  rRect.scale(0.5f, 0.5f);
  EXPECT_FLOAT_EQ(rRect.smoothness(), 0.5f);
  // (1 + s) * (rA + rB) = 1.5 * 60 = 90 still fits the scaled 100 edge.
  EXPECT_LE(1.5f * (rRect.radii()[0].x + rRect.radii()[1].x), rRect.rect().width() + 1e-5f);
  rRect.offset(10, 20);
  EXPECT_FLOAT_EQ(rRect.smoothness(), 0.5f);
}

TGFX_TEST(RRectTest, SmoothnessEquality) {
  const auto rect = Rect::MakeWH(100, 80);
  const auto a = RRect::MakeRectXY(rect, 10, 10, 0.3f);
  const auto same = RRect::MakeRectXY(rect, 10, 10, 0.3f);
  const auto different = RRect::MakeRectXY(rect, 10, 10, 0.4f);
  EXPECT_TRUE(a == same);
  EXPECT_TRUE(a != different);
}

TGFX_TEST(RRectTest, SmoothCornerCoeffs) {
  // Zero smoothness degenerates to a plain circular corner: unit footprint, zero-length
  // transition curves, and a full quarter arc.
  auto coeffs = ComputeSmoothCornerCoeffs(0.0f);
  EXPECT_FLOAT_EQ(coeffs.tangentRatio, 1.f);
  EXPECT_FLOAT_EQ(coeffs.endOffsetX, 0.f);
  EXPECT_FLOAT_EQ(coeffs.endOffsetY, 0.f);
  EXPECT_FLOAT_EQ(coeffs.arcChordRatio, 1.f);
  EXPECT_FLOAT_EQ(coeffs.controlStep, 0.f);
  EXPECT_NEAR(coeffs.arcHandle, 4.f / 3.f * (std::sqrt(2.f) - 1.f), 1e-6f);
  EXPECT_FLOAT_EQ(coeffs.turnSin, 0.f);
  EXPECT_FLOAT_EQ(coeffs.turnCos, 1.f);

  // Full smoothness drops the arc entirely: zero sweep, zero handle, footprint of 2.
  coeffs = ComputeSmoothCornerCoeffs(1.0f);
  EXPECT_FLOAT_EQ(coeffs.tangentRatio, 2.f);
  EXPECT_FLOAT_EQ(coeffs.arcChordRatio, 0.f);
  EXPECT_FLOAT_EQ(coeffs.arcHandle, 0.f);
  EXPECT_NEAR(coeffs.turnSin, 0.70710678f, 1e-6f);
  EXPECT_NEAR(coeffs.turnCos, 0.70710678f, 1e-6f);
  EXPECT_NEAR(coeffs.endOffsetX, 0.29289322f, 1e-6f);
  EXPECT_NEAR(coeffs.endOffsetY, 0.29289322f, 1e-6f);
  EXPECT_NEAR(coeffs.controlStep, 0.47140452f, 1e-6f);

  // Inputs outside [0, 1] are clamped.
  coeffs = ComputeSmoothCornerCoeffs(2.0f);
  EXPECT_FLOAT_EQ(coeffs.tangentRatio, 2.f);
  coeffs = ComputeSmoothCornerCoeffs(-1.0f);
  EXPECT_FLOAT_EQ(coeffs.tangentRatio, 1.f);
}

}  // namespace tgfx
