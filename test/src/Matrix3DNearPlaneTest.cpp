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
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {

// Near-plane distance used by Matrix3D::mapRect() when perspective is present.
constexpr float NearPlaneW = Matrix3D::W_NEAR_PLANE;

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

constexpr auto Square = Rect::MakeLTRB(-50.f, -50.f, 50.f, 50.f);

float CornerW(const Matrix3D& matrix, float x, float y) {
  return matrix.mapHomogeneous(x, y, 0, 1).w;
}

void ExpectProjectedRect(const Matrix3D& matrix, const Rect& src, const Rect& expected) {
  for (const auto& mapped : {matrix.mapRect(src), matrix.asMatrix().mapRect(src)}) {
    EXPECT_TRUE(RectIsFinite(mapped));
    EXPECT_EQ(mapped.isEmpty(), expected.isEmpty());
    EXPECT_FLOAT_EQ(mapped.left, expected.left);
    EXPECT_FLOAT_EQ(mapped.top, expected.top);
    EXPECT_FLOAT_EQ(mapped.right, expected.right);
    EXPECT_FLOAT_EQ(mapped.bottom, expected.bottom);
  }
}

void ExpectCovered(const std::vector<Rect>& rects, const Rect& source) {
  auto covered = false;
  for (const auto& rect : rects) {
    if (rect.contains(source)) {
      covered = true;
      break;
    }
  }
  EXPECT_TRUE(covered);
}

}  // namespace

// Control: a comfortable W (0.5) projects the square to a finite rect scaled by 1/W.
TGFX_TEST(Matrix3DNearPlaneTest, MapRectInFrontOfCamera) {
  const auto matrix = MakePerspectiveDepth(1200.f, 600.f);  // W = 0.5
  ASSERT_FALSE(Matrix3DUtils::IsRectBehindCamera(Square, matrix));

  const auto mapped = matrix.mapRect(Square);
  EXPECT_TRUE(RectIsFinite(mapped));
  EXPECT_FLOAT_EQ(mapped.width(), 200.f);
  EXPECT_FLOAT_EQ(mapped.height(), 200.f);
}

// Control: a rect fully behind the camera (W < 0) is conservatively discarded.
TGFX_TEST(Matrix3DNearPlaneTest, RectBehindCameraIsDiscarded) {
  const auto matrix = MakePerspectiveDepth(1200.f, 2400.f);  // W = -1
  ASSERT_TRUE(Matrix3DUtils::IsRectBehindCamera(Square, matrix));

  auto bounds = Square;
  RegionTransformer::MakeFromMatrix3D(matrix)->transform(&bounds);
  EXPECT_TRUE(bounds.isEmpty());
}

// Bug exposure: all corners have positive W below the near-plane distance (0 < W < 1/2^14).
// IsRectBehindCamera() releases the rect, then MapRectPerspective() clips every corner against
// the near plane and returns a rect of infinities. Expected: mapRect() should return an empty
// rect when every vertex is clipped away by the near plane.
TGFX_TEST(Matrix3DNearPlaneTest, RectInsideNearPlaneMustNotProduceInfinity) {
  const auto eyeDistance = 1200.f;
  const auto depth = eyeDistance * (1.f - 0.5f * NearPlaneW);  // W = 0.5 * (1/2^14) > 0
  const auto matrix = MakePerspectiveDepth(eyeDistance, depth);

  // The case depends on 1 - depth / eyeDistance landing inside the narrow band (0, 2^-14) after
  // rounding; assert the premise so the case fails loudly instead of degrading silently.
  const auto tlW = CornerW(matrix, Square.left, Square.top);
  const auto trW = CornerW(matrix, Square.right, Square.top);
  const auto blW = CornerW(matrix, Square.left, Square.bottom);
  const auto brW = CornerW(matrix, Square.right, Square.bottom);
  ASSERT_GT(tlW, 0.f);
  ASSERT_LT(tlW, NearPlaneW);
  ASSERT_GT(trW, 0.f);
  ASSERT_LT(trW, NearPlaneW);
  ASSERT_GT(blW, 0.f);
  ASSERT_LT(blW, NearPlaneW);
  ASSERT_GT(brW, 0.f);
  ASSERT_LT(brW, NearPlaneW);

  EXPECT_EQ(matrix.mapRect(Square), Rect::MakeEmpty());
  EXPECT_EQ(matrix.asMatrix().mapRect(Square), Rect::MakeEmpty());
  auto bounds = Square;
  matrix.asMatrix().mapRect(&bounds);
  EXPECT_EQ(bounds, Rect::MakeEmpty());

  bounds = Square;
  RegionTransformer::MakeFromMatrix3D(matrix)->transform(&bounds);
  EXPECT_EQ(bounds, Rect::MakeEmpty());
  bounds = Square;
  RegionTransformer::MakeFromMatrix(matrix.asMatrix())->transform(&bounds);
  EXPECT_EQ(bounds, Rect::MakeEmpty());
}

// A rotated rect straddling the near plane: the reference corner is below the near plane while
// its siblings sit just above it. The clip branch divides by the near-plane distance and yields
// a finite but extremely magnified rect.
TGFX_TEST(Matrix3DNearPlaneTest, RectStraddlingNearPlaneStaysFinite) {
  const auto eyeDistance = 1200.f;
  // The center W sits exactly on the near plane; a ~0.042-degree Y rotation shifts the two
  // x-corners by about half the near-plane distance in W, so they straddle the plane.
  constexpr auto tiltDegrees = 0.042f;
  auto model = Matrix3D::MakeTranslate(0.f, 0.f, eyeDistance * (1.f - NearPlaneW));
  model.preRotate({0.f, 1.f, 0.f}, tiltDegrees);
  auto perspective = Matrix3D();
  perspective.setRowColumn(3, 2, -1.f / eyeDistance);
  const auto matrix = perspective * model;

  ASSERT_FALSE(Matrix3DUtils::IsRectBehindCamera(Square, matrix));

  // The tilt angle targets a very narrow floating-point window; assert the straddling premise so
  // the case fails loudly instead of silently degrading into the non-clipped fast path.
  const auto leftW = CornerW(matrix, Square.left, Square.top);
  const auto rightW = CornerW(matrix, Square.right, Square.top);
  ASSERT_TRUE((leftW < NearPlaneW) != (rightW < NearPlaneW))
      << "corners no longer straddle the near plane: leftW = " << leftW << ", rightW = " << rightW;

  const auto mapped = matrix.mapRect(Square);
  EXPECT_FALSE(mapped.isEmpty());
  EXPECT_TRUE(RectIsFinite(mapped))
      << "mapRect returned a non-finite rect: {" << mapped.left << ", " << mapped.top << ", "
      << mapped.right << ", " << mapped.bottom << "}";
}

TGFX_TEST(Matrix3DNearPlaneTest, StraddlingRectRetainsProjectedBounds) {
  const auto src = Rect::MakeLTRB(-1, -1, 1, 1);
  for (int axis = 0; axis < 2; ++axis) {
    for (float direction : {-1.f, 1.f}) {
      auto matrix = Matrix3D::I();
      matrix.setRowColumn(3, axis, direction * NearPlaneW / 2);
      matrix.setRowColumn(3, 3, NearPlaneW);
      const auto farEdge = 1.f / (1.5f * NearPlaneW);
      const auto nearEdge = 1.f / NearPlaneW;
      auto expected = Rect::MakeLTRB(-nearEdge, -nearEdge, nearEdge, nearEdge);
      if (axis == 0) {
        expected.left = direction > 0 ? 0 : -farEdge;
        expected.right = direction > 0 ? farEdge : 0;
      } else {
        expected.top = direction > 0 ? 0 : -farEdge;
        expected.bottom = direction > 0 ? farEdge : 0;
      }
      ExpectProjectedRect(matrix, src, expected);
    }
  }
}

TGFX_TEST(Matrix3DNearPlaneTest, NearPlaneThresholdIsInclusive) {
  const auto src = Rect::MakeLTRB(-1, -1, 1, 1);
  for (float w : {std::nextafter(NearPlaneW, 0.f), NearPlaneW, std::nextafter(NearPlaneW, 1.f)}) {
    auto matrix = Matrix3D::I();
    matrix.setRowColumn(3, 3, w);
    auto expected =
        w < NearPlaneW ? Rect::MakeEmpty() : Rect::MakeLTRB(-1.f / w, -1.f / w, 1.f / w, 1.f / w);
    ExpectProjectedRect(matrix, src, expected);
  }
}

TGFX_TEST(Matrix3DNearPlaneTest, EmptyRegionsStayEmptyThroughEffects) {
  const std::vector<std::shared_ptr<LayerFilter>> filters = {BlurFilter::Make(1, 1)};
  auto outer = RegionTransformer::MakeFromMatrix(Matrix::MakeTrans(20, 30));
  outer = RegionTransformer::MakeFromFilters(filters, 1, outer);
  const auto infinity = std::numeric_limits<float>::infinity();
  for (auto bounds :
       {Rect::MakeEmpty(), Rect::MakeLTRB(infinity, infinity, -infinity, -infinity)}) {
    outer->transform(&bounds);
    EXPECT_EQ(bounds, Rect::MakeEmpty());
  }
  const auto matrix = MakePerspectiveDepth(1024, 1024.f - 1.f / 32.f);
  for (const auto& transformer : {RegionTransformer::MakeFromMatrix(matrix.asMatrix(), outer),
                                  RegionTransformer::MakeFromMatrix3D(matrix, outer),
                                  RegionTransformer::MakeFromClip(Rect::MakeWH(1, 1), outer)}) {
    auto bounds = Rect::MakeLTRB(10, 10, 20, 20);
    transformer->transform(&bounds);
    EXPECT_EQ(bounds, Rect::MakeEmpty());
  }
  auto bounds = Rect::MakeWH(100, 100);
  auto expected = filters.front()->filterBounds(bounds, 1).makeOffset(20, 30);
  outer->transform(&bounds);
  EXPECT_EQ(bounds, expected);
  EXPECT_FALSE(filters.front()->filterBounds(Rect::MakeEmpty(), 1).isEmpty());
}

TGFX_TEST_PRIVATE(Matrix3DNearPlaneTest, LayerDirtyRegionsSurviveNearPlaneTransitions) {
  DisplayList displayList;
  auto parent = Layer::Make();
  parent->setMatrix(Matrix::MakeTrans(1, 1));
  parent->setFilters({BlurFilter::Make(1, 1)});
  displayList.root()->addChild(parent);
  const auto nearMatrix = MakePerspectiveDepth(1024, 1024.f - 1.f / 32.f);
  std::vector<std::shared_ptr<SolidLayer>> children;
  for (int i = 0; i < 4; ++i) {
    auto child = SolidLayer::Make();
    child->setWidth(100);
    child->setHeight(100);
    child->setMatrix3D(nearMatrix);
    parent->addChild(child);
    children.push_back(child);
  }
  std::shared_ptr<RootLayer> root;
  TGFX_PRIVATE_ACCESS(root = displayList._root;)
  EXPECT_TRUE(root->updateDirtyRegions().empty());
  const auto visibleMatrix = MakePerspectiveDepth(1024, 512);
  for (const auto& child : children) {
    child->setMatrix3D(visibleMatrix);
  }
  const auto visibleRegions = root->updateDirtyRegions();
  ASSERT_FALSE(visibleRegions.empty());
  ExpectCovered(visibleRegions, Rect::MakeXYWH(1, 1, 200, 200));
  for (const auto& child : children) {
    child->setMatrix3D(nearMatrix);
  }
  const auto clearedRegions = root->updateDirtyRegions();
  for (const auto& rect : visibleRegions) {
    EXPECT_TRUE(RectIsFinite(rect));
    ExpectCovered(clearedRegions, rect);
  }
  for (const auto& rect : clearedRegions) {
    EXPECT_TRUE(RectIsFinite(rect));
  }
  EXPECT_TRUE(root->updateDirtyRegions().empty());
  for (const auto& child : children) {
    child->setMatrix3D(visibleMatrix);
  }
  const auto restoredRegions = root->updateDirtyRegions();
  for (const auto& rect : visibleRegions) {
    ExpectCovered(restoredRegions, rect);
  }
  for (const auto& rect : restoredRegions) {
    EXPECT_TRUE(RectIsFinite(rect));
  }
}

// Regression guard for double area accumulation: each edge below is 2e19, so a float area product
// (4e38) overflows to infinity and every merge cost degenerates into NaN. The forced-merge
// fallback alone would still converge, so this case also pins down which pair gets merged: the
// adjacent pair at index 1 and 2 costs no extra area and must win over the fallback pair at
// index 0 and 1. Accumulating the areas in float again makes that choice impossible.
TGFX_TEST(Matrix3DNearPlaneTest, AstronomicDirtyRectsKeepRootLayerConverging) {
  const auto edge = 2e19f;
  auto root = RootLayer::Make();
  const Rect sourceRects[] = {Rect::MakeLTRB(0.f, 10.f * edge, edge, 11.f * edge),
                              Rect::MakeLTRB(0.f, 0.f, edge, edge),
                              Rect::MakeLTRB(edge, 0.f, 2.f * edge, edge),
                              Rect::MakeLTRB(0.f, 100.f * edge, edge, 101.f * edge)};
  for (const auto& rect : sourceRects) {
    root->invalidateRect(rect);
  }

  const auto& dirtyRects = root->currentDirtyRects();
  EXPECT_LE(dirtyRects.size(), MAX_DIRTY_REGIONS);
  for (const auto& rect : sourceRects) {
    ExpectCovered(dirtyRects, rect);
  }
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
  std::vector<Rect> sourceRects;
  for (int i = 0; i < 8; i++) {
    const auto top = static_cast<float>(2 * i);
    sourceRects.push_back(Rect::MakeLTRB(-infinity, top, infinity, top + 1.f));
    root->invalidateRect(sourceRects.back());
    EXPECT_LE(root->currentDirtyRects().size(), MAX_DIRTY_REGIONS);
    for (const auto& rect : sourceRects) {
      ExpectCovered(root->currentDirtyRects(), rect);
    }
  }
}

}  // namespace tgfx
