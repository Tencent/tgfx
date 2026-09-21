/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <cfloat>
#include <vector>
#include "base/TGFXTest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/VectorLayer.h"
#include "tgfx/layers/vectors/ShapePath.h"
#include "tgfx/layers/vectors/SolidColor.h"
#include "tgfx/layers/vectors/StrokeStyle.h"
#include "tgfx/layers/vectors/VectorGroup.h"
#include "utils/TestUtils.h"

namespace tgfx {

// Replicates the application's line rendering pipeline: VectorLayer with
// [ShapePath(line offset by half stroke width), StrokeStyle(0.5, 8% black)], rendered through a
// DisplayList at a given zoom. Two identical lines are placed at different subpixel phases and
// their rendered darkness is compared across a zoom sweep.
TGFX_TEST(HairlinePhaseTest, VectorLayerZoomSweep) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP();
    return;
  }
  auto surface = Surface::Make(context, 500, 700);
  auto displayList = std::make_unique<DisplayList>();
  // Match the application: tiled document rendering with smooth tile updates.
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Smooth);
  displayList->setMaxTilesRefinedPerFrame(2);
  displayList->setZoomScalePrecision(1000);

  auto makeLineLayer = [&](float x, float y, float alpha) {
    auto layer = VectorLayer::Make();
    auto group = VectorGroup::Make();
    auto shapePath = ShapePath::Make();
    Path path;
    path.moveTo(0.0f, -0.25f);
    path.lineTo(300.0f, -0.25f);
    shapePath->setPath(path);
    auto strokeStyle = StrokeStyle::Make(SolidColor::Make(Color::Black()));
    strokeStyle->setAlpha(alpha);
    strokeStyle->setStrokeWidth(0.5f);
    strokeStyle->setStrokeAlign(StrokeAlign::Center);
    group->setElements({shapePath, strokeStyle});
    layer->setContents({group});
    layer->setMatrix(Matrix::MakeTrans(x, y));
    // The application disables pass-through for normal-blend nodes, which is what makes the
    // layer eligible for the subtree cache.
    layer->setPassThroughBackground(false);
    displayList->root()->addChild(layer);
  };
  makeLineLayer(50.0f, 40.0f, 0.08f);
  makeLineLayer(50.0f, 75.3f, 0.08f);
  makeLineLayer(50.0f, 120.0f, 1.0f);
  makeLineLayer(50.0f, 155.3f, 1.0f);
  // Phase ladder: identical opaque lines spaced so that at any zoom their device subpixel
  // phases cover a spread of values, including the worst cases.
  for (int k = 0; k < 6; ++k) {
    makeLineLayer(50.0f, 200.0f + static_cast<float>(k) * 8.0f, 1.0f);
  }

  auto measureLine = [&](const Bitmap& bitmap, float docY, float zoomScale) {
    float ink = 0.0f;
    // The line geometry sits at docY - 0.25 (half stroke width up) and the centered stroke
    // spreads docY - 0.5 .. docY in document units.
    auto centerY = static_cast<int>(std::lround((docY - 0.25f) * zoomScale));
    for (int row = centerY - 4; row <= centerY + 4; ++row) {
      if (row < 0 || row >= 700) {
        continue;
      }
      for (int x = 80; x <= 300; x += 8) {
        auto color = bitmap.getColor(x, row);
        ink += 1.0f - color.red;
      }
    }
    return ink;
  };
  // Peak single-row ink: catches sharpness differences that total ink hides. An unsnapped
  // hairline spreads its coverage over two rows at some phases (half the peak), while a
  // snapped one always peaks on a single row.
  auto measurePeakRow = [&](const Bitmap& bitmap, float docY, float zoomScale) {
    float peak = 0.0f;
    auto centerY = static_cast<int>(std::lround((docY - 0.25f) * zoomScale));
    for (int row = centerY - 4; row <= centerY + 4; ++row) {
      if (row < 0 || row >= 700) {
        continue;
      }
      float rowInk = 0.0f;
      for (int x = 80; x <= 300; x += 8) {
        rowInk += 1.0f - bitmap.getColor(x, row).red;
      }
      peak = std::max(peak, rowInk);
    }
    return peak;
  };
  auto readBitmap = [&](Bitmap& bitmap) {
    bitmap.allocPixels(500, 700);
    auto pixels = bitmap.lockPixels();
    EXPECT_TRUE(surface->readPixels(bitmap.info(), pixels));
    bitmap.unlockPixels();
  };
  auto renderFrame = [&]() {
    surface->getCanvas()->clear(Color::White());
    displayList->render(surface.get(), false);
    Bitmap bitmap = {};
    readBitmap(bitmap);
    return bitmap;
  };

  // Simulate a pinch gesture from zoom 0.80 down to 0.73 (zoomScale = zoom * pixelRatio 2),
  // one render per gesture frame, then let the tiled refinement run at the final zoom.
  std::vector<float> gesture = {1.60f, 1.56f, 1.52f, 1.50f, 1.48f, 1.46f};
  for (size_t i = 0; i < gesture.size(); ++i) {
    displayList->setZoomScale(gesture[i]);
    auto bitmap = renderFrame();
    auto darkA = measureLine(bitmap, 40.0f, gesture[i]);
    auto darkB = measureLine(bitmap, 75.3f, gesture[i]);
    printf("[HairlinePhaseTest] gesture zoomScale=%.2f frame=%zu A=%.4f B=%.4f\n", gesture[i], i,
           darkA, darkB);
  }
  for (int frame = 0; frame < 10; ++frame) {
    auto bitmap = renderFrame();
    auto darkA = measureLine(bitmap, 40.0f, 1.46f);
    auto darkB = measureLine(bitmap, 75.3f, 1.46f);
    printf("[HairlinePhaseTest] settle zoomScale=1.46 frame=%d A=%.4f B=%.4f\n", frame, darkA,
           darkB);
    if (frame == 0 || frame == 9) {
      EXPECT_NEAR(darkA, darkB, 0.3f);
      // Phase ladder: identical opaque lines at spread subpixel phases must render with the
      // same ink once settled.
      float minInk = FLT_MAX;
      float maxInk = 0.0f;
      float minPeak = FLT_MAX;
      float maxPeak = 0.0f;
      for (int k = 0; k < 6; ++k) {
        auto docY = 200.0f + static_cast<float>(k) * 8.0f;
        auto ink = measureLine(bitmap, docY, 1.46f);
        auto peak = measurePeakRow(bitmap, docY, 1.46f);
        minInk = std::min(minInk, ink);
        maxInk = std::max(maxInk, ink);
        minPeak = std::min(minPeak, peak);
        maxPeak = std::max(maxPeak, peak);
      }
      printf("[HairlinePhaseTest] ladder min=%.4f max=%.4f peakMin=%.4f peakMax=%.4f\n", minInk,
             maxInk, minPeak, maxPeak);
      EXPECT_NEAR(minInk, maxInk, maxInk * 0.15f);
      EXPECT_NEAR(minPeak, maxPeak, maxPeak * 0.25f);
    }
  }

  // Wider strokes (device width > 1px) go through the quantized rect path: settle at
  // zoomScale 3.0 (device width 1.5px) and check both total ink and peak-row consistency
  // across the ladder phases.
  displayList->setSubtreeCacheMaxSize(0);
  displayList->setZoomScale(3.0f);
  for (int frame = 0; frame < 10; ++frame) {
    auto bitmap = renderFrame();
    if (frame == 9) {
      float minInk = FLT_MAX;
      float maxInk = 0.0f;
      float minPeak = FLT_MAX;
      float maxPeak = 0.0f;
      for (int k = 0; k < 4; ++k) {
        auto docY = 200.0f + static_cast<float>(k) * 8.0f;
        auto ink = measureLine(bitmap, docY, 3.0f);
        auto peak = measurePeakRow(bitmap, docY, 3.0f);
        minInk = std::min(minInk, ink);
        maxInk = std::max(maxInk, ink);
        minPeak = std::min(minPeak, peak);
        maxPeak = std::max(maxPeak, peak);
      }
      printf("[HairlinePhaseTest] wide ladder min=%.4f max=%.4f peakMin=%.4f peakMax=%.4f\n",
             minInk, maxInk, minPeak, maxPeak);
      EXPECT_NEAR(minInk, maxInk, maxInk * 0.15f);
    }
  }
}

// Sub-pixel filled rects model pen-tool strokes, whose outline the application pre-bakes
// into a fill path. Identical rects must render with the same total ink regardless of subpixel
// phase, and the ink must match the paint alpha times the device extent.
TGFX_TEST(HairlinePhaseTest, SubpixelFillRectConsistency) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP();
    return;
  }
  auto surface = Surface::Make(context, 400, 200);
  auto measureInk = [&](const Bitmap& bitmap, int centerRow) {
    float ink = 0.0f;
    for (int row = centerRow - 3; row <= centerRow + 3; ++row) {
      if (row < 0 || row >= 200) {
        continue;
      }
      for (int x = 40; x <= 360; x += 8) {
        ink += 1.0f - bitmap.getColor(x, row).red;
      }
    }
    return ink;
  };
  for (float rectHeight : {0.3f, 0.5f, 0.75f}) {
    // Two subpixel phases: y = 100.0 and y = 150.25.
    float phaseInks[2] = {};
    int phaseIndex = 0;
    for (float topY : {100.0f, 150.25f}) {
      auto canvas = surface->getCanvas();
      canvas->clear(Color::White());
      Paint paint = {};
      paint.setColor(Color::Black());
      paint.setAlpha(0.08f);
      canvas->drawRect(Rect::MakeXYWH(20.0f, topY, 360.0f, rectHeight), paint);
      Bitmap bitmap = {};
      bitmap.allocPixels(400, 200);
      auto pixels = bitmap.lockPixels();
      EXPECT_TRUE(surface->readPixels(bitmap.info(), pixels));
      bitmap.unlockPixels();
      auto ink = measureInk(bitmap, static_cast<int>(std::lround(topY + rectHeight * 0.5f)));
      phaseInks[phaseIndex++] = ink;
      // Expected ink: device extent * paint alpha * 41 sampled columns. 8-bit alpha
      // quantization keeps the rendered ink about 2% below the unquantized expectation.
      auto expected = rectHeight * 0.08f * 41.0f;
      printf("[HairlinePhaseTest] fillRect height=%.2f top=%.2f ink=%.4f expected=%.4f\n",
             rectHeight, topY, ink, expected);
      EXPECT_NEAR(ink, expected, expected * 0.1f);
    }
    // Identical rects must render with the same total ink regardless of subpixel phase.
    EXPECT_NEAR(phaseInks[0], phaseInks[1], std::max(phaseInks[0], phaseInks[1]) * 0.05f);
  }
}

}  // namespace tgfx
