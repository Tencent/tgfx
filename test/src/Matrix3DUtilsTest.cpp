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

#include "core/Matrix3DUtils.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/Rect.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {

constexpr float FootprintEpsilon = 0.01f;

static Matrix3D MakePerspective(float depth) {
  Matrix3D matrix = Matrix3D::I();
  matrix.setRowColumn(3, 2, -1.0f / depth);
  return matrix;
}

}  // namespace

// Near-plane straddle: the bottom corners are behind the camera while the top corners are in
// front. Homogeneous clipping keeps only the finite in-front portion, bounded by the viewport.
TGFX_TEST(Matrix3DUtilsTest, ComputeVisibleFootprintsStraddleNearPlane) {
  const Rect localBounds = Rect::MakeWH(100, 300);
  const Rect destRect = Rect::MakeWH(1000, 1000);
  Matrix3D matrix = Matrix3D::I();
  // W = 1 - y / 200. The top corners have W = 1; the bottom corners have W = -0.5.
  matrix.setRowColumn(3, 1, -1.0f / 200.0f);

  ASSERT_TRUE(Matrix3DUtils::IsRectBehindCamera(localBounds, matrix));
  EXPECT_GT(matrix.mapHomogeneous(0, 0, 0, 1).w, 0.0f);
  EXPECT_LT(matrix.mapHomogeneous(0, 300, 0, 1).w, 0.0f);

  Rect localFootprint;
  Rect destFootprint;
  ASSERT_TRUE(Matrix3DUtils::ComputeVisibleFootprints(localBounds, destRect, matrix,
                                                      &localFootprint, &destFootprint));
  // y = 500 / 3 projects to the viewport bottom (y / (1 - y / 200) = 1000).
  EXPECT_NEAR(localFootprint.left, 0, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.top, 0, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.right, 100, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.bottom, 500.0f / 3.0f, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.left, 0, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.top, 0, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.right, 600, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.bottom, 1000, FootprintEpsilon);
}

// Leaves fully behind the camera or fully outside the viewport have no surviving polygon.
TGFX_TEST(Matrix3DUtilsTest, ComputeVisibleFootprintsCullsInvisibleLeaves) {
  const Rect localBounds = Rect::MakeWH(200, 200);
  const Rect destRect = Rect::MakeWH(600, 600);
  Rect localFootprint;
  Rect destFootprint;

  Matrix3D behindCamera = Matrix3D::MakeTranslate(0, 0, 100);
  behindCamera = MakePerspective(50.0f) * behindCamera;
  EXPECT_FALSE(Matrix3DUtils::ComputeVisibleFootprints(localBounds, destRect, behindCamera,
                                                       &localFootprint, &destFootprint));

  const Matrix3D outsideViewport = Matrix3D::MakeTranslate(10000, 10000, 0);
  EXPECT_FALSE(Matrix3DUtils::ComputeVisibleFootprints(localBounds, destRect, outsideViewport,
                                                       &localFootprint, &destFootprint));
}

// Partial viewport overhang clamps the destination footprint and maps the same surviving region
// back to the corresponding local footprint.
TGFX_TEST(Matrix3DUtilsTest, ComputeVisibleFootprintsClipsPartialViewportOverhang) {
  const Rect localBounds = Rect::MakeWH(100, 100);
  const Rect destRect = Rect::MakeWH(100, 100);
  // Leaf spans dest x in [50, 150], y in [0, 100]: the right half exceeds the viewport.
  const Matrix3D matrix = Matrix3D::MakeTranslate(50, 0, 0);

  Rect localFootprint;
  Rect destFootprint;
  ASSERT_TRUE(Matrix3DUtils::ComputeVisibleFootprints(localBounds, destRect, matrix,
                                                      &localFootprint, &destFootprint));
  EXPECT_NEAR(destFootprint.left, 50, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.right, 100, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.top, 0, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.bottom, 100, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.left, 0, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.right, 50, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.top, 0, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.bottom, 100, FootprintEpsilon);
}

// A fully visible projective leaf validates the local/destination footprint pairing. Both outputs
// describe the same local rect, with destination coordinates after perspective divide.
TGFX_TEST(Matrix3DUtilsTest, ComputeVisibleFootprintsKeepsProjectiveFootprintsPaired) {
  const Rect localBounds = Rect::MakeWH(100, 100);
  const Rect destRect = Rect::MakeWH(200, 200);
  Matrix3D matrix = Matrix3D::I();
  // W = 1 - y / 400 stays positive over localBounds; the bottom-right corner (100, 100)
  // projects to (400/3, 400/3), setting both the destination right and bottom edges.
  matrix.setRowColumn(3, 1, -1.0f / 400.0f);

  Rect localFootprint;
  Rect destFootprint;
  ASSERT_TRUE(Matrix3DUtils::ComputeVisibleFootprints(localBounds, destRect, matrix,
                                                      &localFootprint, &destFootprint));
  EXPECT_NEAR(localFootprint.left, 0, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.top, 0, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.right, 100, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.bottom, 100, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.left, 0, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.top, 0, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.right, 400.0f / 3.0f, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.bottom, 400.0f / 3.0f, FootprintEpsilon);
}

// A singular local-to-destination homography cannot yield paired footprints.
TGFX_TEST(Matrix3DUtilsTest, ComputeVisibleFootprintsRejectsSingularHomography) {
  const Rect localBounds = Rect::MakeWH(100, 100);
  const Rect destRect = Rect::MakeWH(200, 200);
  Matrix3D matrix = Matrix3D::I();
  matrix.setRowColumn(0, 0, 0);

  Rect localFootprint;
  Rect destFootprint;
  EXPECT_FALSE(Matrix3DUtils::ComputeVisibleFootprints(localBounds, destRect, matrix,
                                                       &localFootprint, &destFootprint));
}

}  // namespace tgfx
