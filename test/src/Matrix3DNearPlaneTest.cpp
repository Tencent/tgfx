/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
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
#include <limits>
#include "base/TGFXTest.h"
#include "core/Matrix3DUtils.h"
#include "gtest/gtest.h"
#include "layers/RegionTransformer.h"
#include "layers/RootLayer.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

namespace {

// Near-plane distance used by Matrix3D::mapRect() when perspective is present.
constexpr float kNearPlaneW = Matrix3D::W_NEAR_PLANE;

bool RectIsFinite(const Rect& rect) {
  return std::isfinite(rect.left) && std::isfinite(rect.top) && std::isfinite(rect.right) &&
         std::isfinite(rect.bottom);
}

// Builds a "perspective(eyeDistance) x translateZ(depth)" matrix. A local point (x, y, 0) is
// projected with the homogeneous component W = 1 - depth / eyeDistance.
Matrix3D MakePerspectiveDepth(float eyeDistance, float depth) {
  auto model = Matrix3D::MakeTranslate(0.f, 0.f, depth);
  auto perspective = Matrix3D();
  perspective.setRowColumn(3, 2, -1.f / eyeDistance);
  return perspective * model;
}

constexpr auto kSquare = Rect::MakeLTRB(-50.f, -50.f, 50.f, 50.f);

float CornerW(const Matrix3D& matrix, float x, float y) {
  return matrix.mapHomogeneous(x, y, 0, 1).w;
}

}  // namespace

// Control: a comfortable W (0.5) projects the square to a finite rect scaled by 1/W.
TGFX_TEST(Matrix3DNearPlaneTest, MapRectInFrontOfCamera) {
  const auto matrix = MakePerspectiveDepth(1200.f, 600.f);  // W = 0.5
  ASSERT_FALSE(Matrix3DUtils::IsRectBehindCamera(kSquare, matrix));

  const auto mapped = matrix.mapRect(kSquare);
  EXPECT_TRUE(RectIsFinite(mapped));
  EXPECT_FLOAT_EQ(mapped.width(), 200.f);
  EXPECT_FLOAT_EQ(mapped.height(), 200.f);
}

// Control: a rect fully behind the camera (W < 0) is conservatively discarded.
TGFX_TEST(Matrix3DNearPlaneTest, RectBehindCameraIsDiscarded) {
  const auto matrix = MakePerspectiveDepth(1200.f, 2400.f);  // W = -1
  ASSERT_TRUE(Matrix3DUtils::IsRectBehindCamera(kSquare, matrix));

  auto bounds = kSquare;
  RegionTransformer::MakeFromMatrix3D(matrix)->transform(&bounds);
  EXPECT_TRUE(bounds.isEmpty());
}

// Bug exposure: all corners have positive W below the near-plane distance (0 < W < 1/2^14).
// IsRectBehindCamera() releases the rect, then MapRectPerspective() clips every corner against
// the near plane and returns a rect of infinities. Expected: mapRect() should return an empty
// rect when every vertex is clipped away by the near plane.
TGFX_TEST(Matrix3DNearPlaneTest, RectInsideNearPlaneMustNotProduceInfinity) {
  const auto eyeDistance = 1200.f;
  const auto depth = eyeDistance * (1.f - 0.5f * kNearPlaneW);  // W = 0.5 * (1/2^14) > 0
  const auto matrix = MakePerspectiveDepth(eyeDistance, depth);

  // The case depends on 1 - depth / eyeDistance landing inside the narrow band (0, 2^-14) after
  // rounding; assert the premise so the case fails loudly instead of degrading silently.
  const auto tlW = CornerW(matrix, kSquare.left, kSquare.top);
  const auto trW = CornerW(matrix, kSquare.right, kSquare.top);
  const auto blW = CornerW(matrix, kSquare.left, kSquare.bottom);
  const auto brW = CornerW(matrix, kSquare.right, kSquare.bottom);
  ASSERT_GT(tlW, 0.f);
  ASSERT_LT(tlW, kNearPlaneW);
  ASSERT_GT(trW, 0.f);
  ASSERT_LT(trW, kNearPlaneW);
  ASSERT_GT(blW, 0.f);
  ASSERT_LT(blW, kNearPlaneW);
  ASSERT_GT(brW, 0.f);
  ASSERT_LT(brW, kNearPlaneW);

  const auto mapped = matrix.mapRect(kSquare);
  EXPECT_TRUE(RectIsFinite(mapped))
      << "mapRect returned a non-finite rect: {" << mapped.left << ", " << mapped.top << ", "
      << mapped.right << ", " << mapped.bottom << "}";

  auto bounds = kSquare;
  RegionTransformer::MakeFromMatrix3D(matrix)->transform(&bounds);
  EXPECT_TRUE(RectIsFinite(bounds) || bounds.isEmpty())
      << "RegionTransformer produced a non-finite dirty rect: {" << bounds.left << ", "
      << bounds.top << ", " << bounds.right << ", " << bounds.bottom << "}";
}

// A rotated rect straddling the near plane: the reference corner is below the near plane while
// its siblings sit just above it. The clip branch divides by the near-plane distance and yields
// a finite but extremely magnified rect.
TGFX_TEST(Matrix3DNearPlaneTest, RectStraddlingNearPlaneStaysFinite) {
  const auto eyeDistance = 1200.f;
  // The center W sits exactly on the near plane; a ~0.042-degree Y rotation shifts the two
  // x-corners by about half the near-plane distance in W, so they straddle the plane.
  constexpr auto tiltDegrees = 0.042f;
  auto model = Matrix3D::MakeTranslate(0.f, 0.f, eyeDistance * (1.f - kNearPlaneW));
  model.preRotate({0.f, 1.f, 0.f}, tiltDegrees);
  auto perspective = Matrix3D();
  perspective.setRowColumn(3, 2, -1.f / eyeDistance);
  const auto matrix = perspective * model;

  ASSERT_FALSE(Matrix3DUtils::IsRectBehindCamera(kSquare, matrix));

  // The tilt angle targets a very narrow floating-point window; assert the straddling premise so
  // the case fails loudly instead of silently degrading into the non-clipped fast path.
  const auto leftW = CornerW(matrix, kSquare.left, kSquare.top);
  const auto rightW = CornerW(matrix, kSquare.right, kSquare.top);
  ASSERT_TRUE((leftW < kNearPlaneW) != (rightW < kNearPlaneW))
      << "corners no longer straddle the near plane: leftW = " << leftW << ", rightW = " << rightW;

  const auto mapped = matrix.mapRect(kSquare);
  EXPECT_TRUE(RectIsFinite(mapped))
      << "mapRect returned a non-finite rect: {" << mapped.left << ", " << mapped.top << ", "
      << mapped.right << ", " << mapped.bottom << "}";
}

// Regression guard for double area accumulation: each edge below is 2e19, so a float area product
// (4e38) overflows to infinity and every merge cost degenerates into NaN. The forced-merge
// fallback alone would still converge, so this case also pins down which pair gets merged: the
// adjacent pair at index 1 and 2 costs no extra area and must win over the fallback pair at
// index 0 and 1. Accumulating the areas in float again makes that choice impossible.
TGFX_TEST(Matrix3DNearPlaneTest, AstronomicDirtyRectsKeepRootLayerConverging) {
  const auto edge = 2e19f;
  auto root = RootLayer::Make();
  // Index 0: far away on the y axis, expensive to merge with anything.
  root->invalidateRect(Rect::MakeLTRB(0.f, 10.f * edge, edge, 11.f * edge));
  // Index 1 and 2: horizontally adjacent, so their union adds no area at all.
  root->invalidateRect(Rect::MakeLTRB(0.f, 0.f, edge, edge));
  root->invalidateRect(Rect::MakeLTRB(edge, 0.f, 2.f * edge, edge));
  // Index 3: forces a merge by pushing the list to MAX_DIRTY_REGIONS + 1.
  root->invalidateRect(Rect::MakeLTRB(0.f, 100.f * edge, edge, 101.f * edge));

  const auto& dirtyRects = root->currentDirtyRects();
  EXPECT_LE(dirtyRects.size(), MAX_DIRTY_REGIONS);
  const auto cheapestUnion = Rect::MakeLTRB(0.f, 0.f, 2.f * edge, edge);
  auto mergedCheapestPair = false;
  for (const auto& rect : dirtyRects) {
    EXPECT_TRUE(RectIsFinite(rect));
    if (rect == cheapestUnion) {
      mergedCheapestPair = true;
    }
  }
  EXPECT_TRUE(mergedCheapestPair)
      << "forced merge did not pick the zero-cost pair; merge costs are no longer comparable";
}

// Regression guard for the forced-merge fallback pair: an infinite extent is not filtered out by
// invalidateRect() because left < right still holds, so every area and every union area is
// infinite and every merge cost degenerates into NaN, losing all comparisons. Without the fallback
// no pair is ever selected and the dirty list stops converging.
TGFX_TEST(Matrix3DNearPlaneTest, NonFiniteDirtyRectsStillConverge) {
  const auto infinity = std::numeric_limits<float>::infinity();
  auto root = RootLayer::Make();
  for (int i = 0; i < 4; i++) {
    const auto top = static_cast<float>(2 * i);
    root->invalidateRect(Rect::MakeLTRB(-infinity, top, infinity, top + 1.f));
  }
  EXPECT_LE(root->currentDirtyRects().size(), MAX_DIRTY_REGIONS);
}

}  // namespace tgfx
