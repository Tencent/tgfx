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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "core/Matrix3DUtils.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/Rect.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {

// The raster dimensions a scale-then-round sizing of the full local bounds would produce:
//   scaledBounds = localBounds; scaledBounds.scale(contentScale, contentScale); roundOut;
//   raster = ceil(scaledBounds.width x height)
struct OldSizing {
  int rasterWidth;
  int rasterHeight;
};

static OldSizing ComputeOldSizing(const Rect& localBounds, float contentScale) {
  Rect scaled = localBounds;
  scaled.scale(contentScale, contentScale);
  scaled.roundOut();
  return {static_cast<int>(scaled.width()), static_cast<int>(scaled.height())};
}

// The raster dimensions the footprint-based sizing chooses, in the same units:
//   compute the homogeneous-clipped local and destination footprints,
//   raster = ceil(destFootprint.size).
struct NewSizing {
  bool visible;
  int rasterWidth;
  int rasterHeight;
  float densityX;
  float densityY;
  Rect localFootprint;
  Rect destFootprint;
};

static NewSizing ComputeNewSizing(const Rect& localBounds, const Matrix3D& localToCompositor,
                                  const Rect& compositorViewport) {
  NewSizing out{};
  Rect localFootprint = {};
  Rect destFootprint = {};
  if (Matrix3DUtils::IsRectBehindCamera(localBounds, localToCompositor)) {
    return out;
  }
  if (!Matrix3DUtils::ComputeVisibleFootprints(localBounds, compositorViewport, localToCompositor,
                                               &localFootprint, &destFootprint)) {
    return out;
  }
  if (!localFootprint.intersect(localBounds)) {
    return out;
  }
  localFootprint.roundOut();
  destFootprint.roundOut();
  const float localWidth = localFootprint.width();
  const float localHeight = localFootprint.height();
  const float destWidth = destFootprint.width();
  const float destHeight = destFootprint.height();
  if (!(localWidth > 0.0f) || !(localHeight > 0.0f) || !(destWidth > 0.0f) ||
      !(destHeight > 0.0f)) {
    return out;
  }
  out.visible = true;
  out.densityX = destWidth / localWidth;
  out.densityY = destHeight / localHeight;
  out.rasterWidth = std::max(1, static_cast<int>(std::ceil(destWidth)));
  out.rasterHeight = std::max(1, static_cast<int>(std::ceil(destHeight)));
  out.localFootprint = localFootprint;
  out.destFootprint = destFootprint;
  return out;
}

// Build the local->compositor matrix the same way Render3DContext::finishAndDrawTo does:
//   node.transform * postScale(contentScale) * postTranslate(-renderRect.tl)
static Matrix3D MakeLocalToCompositor(const Matrix3D& nodeTransform, float contentScale,
                                      const Rect& renderRect) {
  Matrix3D m = nodeTransform;
  m.postScale(contentScale, contentScale, 1.0f);
  m.postTranslate(-renderRect.left, -renderRect.top, 0);
  return m;
}

// Perspective matrix with the convention used in tgfx 3D tests: sets row 3, column 2 to -1/depth,
// so a local point maps to W = w - z/depth and recedes behind the camera when W <= 0.
static Matrix3D MakePerspective(float depth) {
  Matrix3D m = Matrix3D::I();
  m.setRowColumn(3, 2, -1.0f / depth);
  return m;
}

// Common label + numbers dump so test output reads like a small comparison table.
static void ReportRow(const char* label, const OldSizing& oldSize, const NewSizing& newSize) {
  std::printf("[Case] %s\n", label);
  std::printf("  old raster: %d x %d\n", oldSize.rasterWidth, oldSize.rasterHeight);
  if (newSize.visible) {
    std::printf("  new raster: %d x %d  (density=%.3f x %.3f)\n", newSize.rasterWidth,
                newSize.rasterHeight, newSize.densityX, newSize.densityY);
    std::printf("  visible local: [%.2f, %.2f, %.2f, %.2f]\n", newSize.localFootprint.left,
                newSize.localFootprint.top, newSize.localFootprint.right,
                newSize.localFootprint.bottom);
    std::printf("  projected dest: [%.2f, %.2f, %.2f, %.2f]\n", newSize.destFootprint.left,
                newSize.destFootprint.top, newSize.destFootprint.right,
                newSize.destFootprint.bottom);
    const double oldPixels = static_cast<double>(oldSize.rasterWidth) * oldSize.rasterHeight;
    const double newPixels = static_cast<double>(newSize.rasterWidth) * newSize.rasterHeight;
    if (newPixels > 0.0) {
      std::printf("  raster shrink: %.1fx  (%.0f -> %.0f pixels)\n", oldPixels / newPixels,
                  oldPixels, newPixels);
    }
  } else {
    std::printf("  new: culled (invisible)\n");
  }
}

// Backing surfaces must fit within GPU texture size limits (16384 per axis on Metal/Vulkan, 8192
// on WebGPU). The scale-then-round rasters below exceed 16384, while the footprint-based rasters
// stay far below it; asserting against 16384 keeps the bound on the Metal/Vulkan limit.
constexpr int SurfaceMakeLimit = 16384;

}  // namespace

// ---------------------------------------------------------------------------------------------
// Baseline sanity: a flat leaf with a pure 2D transform (translation only). Both sizings should
// agree when there is no rotation or perspective.
// ---------------------------------------------------------------------------------------------
TGFX_TEST(Render3DRasterSizingTest, FlatLeaf_MatchesOldAlgorithm) {
  const Rect localBounds = Rect::MakeWH(100, 100);
  const Matrix3D nodeTransform = Matrix3D::MakeTranslate(50, 50, 0);
  const float contentScale = 1.0f;
  const Rect renderRect = Rect::MakeWH(200, 200);
  const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());

  auto localToCompositor = MakeLocalToCompositor(nodeTransform, contentScale, renderRect);
  auto oldSize = ComputeOldSizing(localBounds, contentScale);
  auto newSize = ComputeNewSizing(localBounds, localToCompositor, viewport);
  ReportRow("FlatLeaf_Identity", oldSize, newSize);

  ASSERT_TRUE(newSize.visible);
  EXPECT_EQ(newSize.rasterWidth, oldSize.rasterWidth);
  EXPECT_EQ(newSize.rasterHeight, oldSize.rasterHeight);
}

// ---------------------------------------------------------------------------------------------
// A large leaf under a very high content scale: the leaf maps to far more compositor area than
// is visible. The scale-then-round sizing allocates a per-axis raster of localBounds x
// contentScale (≈1.2M per axis here), far beyond the viewport; the footprint-based sizing
// allocates only the visible portion and stays at viewport scale.
// ---------------------------------------------------------------------------------------------
TGFX_TEST(Render3DRasterSizingTest, ExtremeZoom_ShrinksRasterByOrdersOfMagnitude) {
  const Rect localBounds = Rect::MakeWH(1556, 1556);
  const Matrix3D nodeTransform = Matrix3D::I();
  const float contentScale = 783.0f;
  const Rect renderRect = Rect::MakeWH(1861, 1861);
  const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());

  auto localToCompositor = MakeLocalToCompositor(nodeTransform, contentScale, renderRect);
  auto oldSize = ComputeOldSizing(localBounds, contentScale);
  auto newSize = ComputeNewSizing(localBounds, localToCompositor, viewport);
  ReportRow("ExtremeZoom_1556x1556_scale783", oldSize, newSize);

  ASSERT_TRUE(newSize.visible);
  // Scale-then-round sizing: 1556 * 783 ≈ 1.2M per axis, far exceeding the viewport dimensions.
  EXPECT_GT(static_cast<float>(oldSize.rasterWidth), viewport.width() * 100);
  EXPECT_GT(static_cast<float>(oldSize.rasterHeight), viewport.height() * 100);
  // Footprint-based sizing: the raster stays at viewport scale (in fact ≤ viewport dimensions).
  EXPECT_LE(static_cast<float>(newSize.rasterWidth), viewport.width() + 1);
  EXPECT_LE(static_cast<float>(newSize.rasterHeight), viewport.height() + 1);
}

// ---------------------------------------------------------------------------------------------
// A face strongly foreshortened by a perspective rotation. The scale-then-round sizing still
// allocates localBounds x contentScale (20000 per axis here), exceeding the GPU texture limit;
// the footprint-based sizing produces a raster proportional to the on-screen coverage.
// ---------------------------------------------------------------------------------------------
TGFX_TEST(Render3DRasterSizingTest, PerspectiveShrunkFace_AvoidsOversizedTexture) {
  const Rect localBounds = Rect::MakeWH(200, 200);
  // Rotate around Y so the face tilts strongly, then add perspective so far corners recede.
  Matrix3D nodeTransform = Matrix3D::MakeRotate({0, 1, 0}, 70.0f);
  nodeTransform = MakePerspective(80.0f) * nodeTransform;
  const float contentScale = 100.0f;
  const Rect renderRect = Rect::MakeWH(2000, 2000);
  const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());

  auto localToCompositor = MakeLocalToCompositor(nodeTransform, contentScale, renderRect);
  auto oldSize = ComputeOldSizing(localBounds, contentScale);
  auto newSize = ComputeNewSizing(localBounds, localToCompositor, viewport);
  ReportRow("PerspectiveShrunk_rotY70_persp80_scale100", oldSize, newSize);

  // Scale-then-round sizing: 200 * 100 = 20000 per axis regardless of on-screen coverage.
  EXPECT_EQ(oldSize.rasterWidth, 20000);
  EXPECT_EQ(oldSize.rasterHeight, 20000);
  // The scale-then-round raster far exceeds the GPU max texture size, so Surface::Make would fail.
  EXPECT_GT(oldSize.rasterWidth, SurfaceMakeLimit);
  EXPECT_GT(oldSize.rasterHeight, SurfaceMakeLimit);
  if (newSize.visible) {
    // The footprint-based raster fits within GPU limits and stays below viewport scale.
    EXPECT_LE(newSize.rasterWidth, SurfaceMakeLimit);
    EXPECT_LE(newSize.rasterHeight, SurfaceMakeLimit);
    EXPECT_LT(newSize.rasterWidth, oldSize.rasterWidth);
    EXPECT_LT(newSize.rasterHeight, oldSize.rasterHeight);
  }
}

// ---------------------------------------------------------------------------------------------
// A severely tilted face with a very large contentScale, mimicking a camera close to the leaf
// plane. The scale-then-round sizing allocates 372000 per axis, far beyond any GPU texture limit;
// the footprint-based sizing must keep the raster bounded.
// ---------------------------------------------------------------------------------------------
TGFX_TEST(Render3DRasterSizingTest, PathologicalTilt_NewRasterStaysUnderGpuLimit) {
  const Rect localBounds = Rect::MakeWH(200, 200);
  // Rotate around X and Y and add strong perspective so w' -> 0 along the leaf's far edge.
  Matrix3D nodeTransform = Matrix3D::MakeRotate({1, 1, 0}, 80.0f);
  nodeTransform = MakePerspective(150.0f) * nodeTransform;
  const float contentScale = 1860.0f;
  const Rect renderRect = Rect::MakeWH(1861, 1861);
  const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());

  auto localToCompositor = MakeLocalToCompositor(nodeTransform, contentScale, renderRect);
  auto oldSize = ComputeOldSizing(localBounds, contentScale);
  auto newSize = ComputeNewSizing(localBounds, localToCompositor, viewport);
  ReportRow("PathologicalTilt_rotXY80_scale1860", oldSize, newSize);

  // Scale-then-round raster: 200 * 1860 = 372000 per axis. Extreme over-allocation.
  EXPECT_EQ(oldSize.rasterWidth, 372000);
  EXPECT_EQ(oldSize.rasterHeight, 372000);
  EXPECT_GT(oldSize.rasterWidth, SurfaceMakeLimit * 10);
  if (newSize.visible) {
    // The footprint-based raster fits within GPU limits.
    EXPECT_LE(newSize.rasterWidth, SurfaceMakeLimit);
    EXPECT_LE(newSize.rasterHeight, SurfaceMakeLimit);
  }
}

// ---------------------------------------------------------------------------------------------
// A leaf completely outside the viewport: the scale-then-round sizing still allocates a
// full-size raster; the footprint-based sizing culls it entirely.
// ---------------------------------------------------------------------------------------------
TGFX_TEST(Render3DRasterSizingTest, LeafOutsideViewport_NewAlgorithmCulls) {
  const Rect localBounds = Rect::MakeWH(200, 200);
  // Translate far outside the viewport in world space.
  const Matrix3D nodeTransform = Matrix3D::MakeTranslate(10000, 10000, 0);
  const float contentScale = 1.0f;
  const Rect renderRect = Rect::MakeWH(500, 500);
  const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());

  auto localToCompositor = MakeLocalToCompositor(nodeTransform, contentScale, renderRect);
  auto oldSize = ComputeOldSizing(localBounds, contentScale);
  auto newSize = ComputeNewSizing(localBounds, localToCompositor, viewport);
  ReportRow("LeafOutsideViewport", oldSize, newSize);

  // Scale-then-round sizing: allocates a 200x200 raster even though nothing is visible.
  EXPECT_EQ(oldSize.rasterWidth, 200);
  EXPECT_EQ(oldSize.rasterHeight, 200);
  // Footprint-based sizing: culls the leaf entirely.
  EXPECT_FALSE(newSize.visible);
}

// ---------------------------------------------------------------------------------------------
// A leaf fully behind the camera (every vertex has w <= 0). The scale-then-round sizing still
// allocates a full-size raster; the footprint-based sizing culls it via IsRectBehindCamera.
// ---------------------------------------------------------------------------------------------
TGFX_TEST(Render3DRasterSizingTest, LeafBehindCamera_NewAlgorithmCulls) {
  const Rect localBounds = Rect::MakeWH(200, 200);
  // Push the leaf far enough along +Z that the perspective divide produces w <= 0 at every
  // corner (perspective factor is -1/50, so z = 50 puts w exactly at 0; z = 100 puts w = -1).
  Matrix3D nodeTransform = Matrix3D::MakeTranslate(0, 0, 100);
  nodeTransform = MakePerspective(50.0f) * nodeTransform;
  const float contentScale = 1.0f;
  const Rect renderRect = Rect::MakeWH(500, 500);
  const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());

  auto localToCompositor = MakeLocalToCompositor(nodeTransform, contentScale, renderRect);
  auto oldSize = ComputeOldSizing(localBounds, contentScale);
  auto newSize = ComputeNewSizing(localBounds, localToCompositor, viewport);
  ReportRow("LeafBehindCamera", oldSize, newSize);

  EXPECT_EQ(oldSize.rasterWidth, 200);
  EXPECT_EQ(oldSize.rasterHeight, 200);
  EXPECT_FALSE(newSize.visible);
}

// ---------------------------------------------------------------------------------------------
// A face with strongly non-uniform on-screen extent (rotation plus perspective distortion).
// The scale-then-round sizing produces a symmetric raster; the footprint-based sizing yields
// different X/Y densities that match the actual projected footprint.
// ---------------------------------------------------------------------------------------------
TGFX_TEST(Render3DRasterSizingTest, AnisotropicProjection_ProducesAnisotropicRaster) {
  const Rect localBounds = Rect::MakeWH(200, 200);
  Matrix3D nodeTransform = Matrix3D::MakeRotate({0, 1, 0}, 45.0f);
  nodeTransform = MakePerspective(200.0f) * nodeTransform;
  const float contentScale = 5.0f;
  const Rect renderRect = Rect::MakeWH(2000, 2000);
  const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());

  auto localToCompositor = MakeLocalToCompositor(nodeTransform, contentScale, renderRect);
  auto oldSize = ComputeOldSizing(localBounds, contentScale);
  auto newSize = ComputeNewSizing(localBounds, localToCompositor, viewport);
  ReportRow("AnisotropicProjection_rotY45_persp200", oldSize, newSize);

  // Scale-then-round sizing is isotropic — width and height both = 200*5 = 1000.
  EXPECT_EQ(oldSize.rasterWidth, 1000);
  EXPECT_EQ(oldSize.rasterHeight, 1000);
  ASSERT_TRUE(newSize.visible);
  // The footprint-based density is anisotropic (different X vs Y), reflecting the actual
  // projected footprint.
  const float aspectRatio =
      static_cast<float>(newSize.rasterWidth) / static_cast<float>(newSize.rasterHeight);
  EXPECT_NE(aspectRatio, 1.0f);
}

}  // namespace tgfx
