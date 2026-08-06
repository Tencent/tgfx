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

#include "layers/compositing3d/Render3DContext.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/Rect.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {

static Matrix3D MakeLocalToCompositor(const Matrix3D& nodeTransform, float contentScale,
                                      const Rect& renderRect) {
  Matrix3D matrix = nodeTransform;
  matrix.postScale(contentScale, contentScale, 1.0f);
  matrix.postTranslate(-renderRect.left, -renderRect.top, 0);
  return matrix;
}

static Matrix3D MakePerspective(float depth) {
  Matrix3D matrix = Matrix3D::I();
  matrix.setRowColumn(3, 2, -1.0f / depth);
  return matrix;
}

}  // namespace

// A flat leaf preserves the main-branch contentScale sizing when the visible footprint covers the
// complete local bounds.
TGFX_TEST_PRIVATE(Render3DRasterSizingTest, FlatLeaf_PreservesContentScaleSizing) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(100, 100);
    const float contentScale = 1.0f;
    const Rect renderRect = Rect::MakeWH(200, 200);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor =
        MakeLocalToCompositor(Matrix3D::MakeTranslate(50, 50, 0), contentScale, renderRect);
    Render3DContext::RasterInfo info;

    ASSERT_TRUE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                   contentScale, &info));
    EXPECT_EQ(info.visibleLocal, localBounds);
    EXPECT_FLOAT_EQ(info.density.getScaleX(), contentScale);
    EXPECT_FLOAT_EQ(info.density.getScaleY(), contentScale);
    EXPECT_EQ(info.rasterWidth, 100);
    EXPECT_EQ(info.rasterHeight, 100);
  });
}

// A large leaf under an extreme content scale is cropped to the compositor viewport before
// rasterization, avoiding the main-branch allocation of more than one million pixels per axis.
TGFX_TEST_PRIVATE(Render3DRasterSizingTest, ExtremeZoom_CropsToViewportScale) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(1556, 1556);
    const float contentScale = 783.0f;
    const Rect renderRect = Rect::MakeWH(1861, 1861);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor = MakeLocalToCompositor(Matrix3D::I(), contentScale, renderRect);
    Render3DContext::RasterInfo info;

    ASSERT_TRUE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                   contentScale, &info));
    EXPECT_LT(info.visibleLocal.width(), localBounds.width());
    EXPECT_LT(info.visibleLocal.height(), localBounds.height());
    EXPECT_LE(static_cast<float>(info.rasterWidth), viewport.width() + 1.0f);
    EXPECT_LE(static_cast<float>(info.rasterHeight), viewport.height() + 1.0f);
  });
}

// A fully visible oversized leaf deliberately retains the main-branch contentScale sizing. It is
// outside this PR's viewport-clipping scope and can still exceed the GPU texture limit.
TGFX_TEST_PRIVATE(Render3DRasterSizingTest, FullyVisibleOversizedLeaf_PreservesMainBehavior) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(200, 200);
    const float contentScale = 100.0f;
    const Rect renderRect = Rect::MakeWH(30000, 30000);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor = MakeLocalToCompositor(Matrix3D::I(), contentScale, renderRect);
    Render3DContext::RasterInfo info;

    ASSERT_TRUE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                   contentScale, &info));
    EXPECT_EQ(info.visibleLocal, localBounds);
    EXPECT_FLOAT_EQ(info.density.getScaleX(), contentScale);
    EXPECT_FLOAT_EQ(info.density.getScaleY(), contentScale);
    EXPECT_EQ(info.rasterWidth, 20000);
    EXPECT_EQ(info.rasterHeight, 20000);
  });
}

// A leaf that crosses the near plane is not rasterized. Partial near-plane clipping is not in this
// PR's scope.
TGFX_TEST_PRIVATE(Render3DRasterSizingTest, NearPlaneStraddle_CullsLeaf) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(100, 300);
    Matrix3D nodeTransform = Matrix3D::I();
    nodeTransform.setRowColumn(3, 1, -1.0f / 200.0f);
    const float contentScale = 1.0f;
    const Rect renderRect = Rect::MakeWH(1000, 1000);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor = MakeLocalToCompositor(nodeTransform, contentScale, renderRect);
    Render3DContext::RasterInfo info;

    EXPECT_FALSE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                    contentScale, &info));
  });
}

TGFX_TEST_PRIVATE(Render3DRasterSizingTest, LeafOutsideViewport_CullsLeaf) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(200, 200);
    const float contentScale = 1.0f;
    const Rect renderRect = Rect::MakeWH(500, 500);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor =
        MakeLocalToCompositor(Matrix3D::MakeTranslate(10000, 10000, 0), contentScale, renderRect);
    Render3DContext::RasterInfo info;

    EXPECT_FALSE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                    contentScale, &info));
  });
}

TGFX_TEST_PRIVATE(Render3DRasterSizingTest, LeafBehindCamera_CullsLeaf) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(200, 200);
    Matrix3D nodeTransform = Matrix3D::MakeTranslate(0, 0, 100);
    nodeTransform = MakePerspective(50.0f) * nodeTransform;
    const float contentScale = 1.0f;
    const Rect renderRect = Rect::MakeWH(500, 500);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor = MakeLocalToCompositor(nodeTransform, contentScale, renderRect);
    Render3DContext::RasterInfo info;

    EXPECT_FALSE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                    contentScale, &info));
  });
}

// A leaf clipped only along the horizontal axis keeps the untouched vertical range and derives a
// projected-footprint density distinct from the horizontal one.
TGFX_TEST_PRIVATE(Render3DRasterSizingTest, ClippedAnisotropicProjection_UsesProjectedDensity) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(200, 200);
    Matrix3D nodeTransform = Matrix3D::MakeRotate({0, 1, 0}, 45.0f);
    nodeTransform = MakePerspective(200.0f) * nodeTransform;
    const float contentScale = 5.0f;
    const Rect renderRect = Rect::MakeWH(2000, 2000);
    // The projected leaf spans dest x in [261, 547] and y in [369, 774], so a 400-wide viewport
    // clips only the horizontal axis.
    const Rect viewport = Rect::MakeWH(400, 2000);
    const auto localToCompositor = MakeLocalToCompositor(nodeTransform, contentScale, renderRect);
    Render3DContext::RasterInfo info;

    ASSERT_TRUE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                   contentScale, &info));
    EXPECT_LT(info.visibleLocal.width(), localBounds.width());
    EXPECT_EQ(info.visibleLocal.height(), localBounds.height());

    EXPECT_NE(info.density.getScaleX(), info.density.getScaleY());
    EXPECT_LE(static_cast<float>(info.rasterWidth), viewport.width() + 1.0f);
    EXPECT_LE(static_cast<float>(info.rasterHeight), viewport.height() + 1.0f);
  });
}

}  // namespace tgfx
