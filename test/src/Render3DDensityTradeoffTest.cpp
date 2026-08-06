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

#include "tgfx/core/Canvas.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/Layer.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {

// The review scenario: a long floor receding along the view direction with a strong perspective.
// The near edge (z' = 400) renders at about 5x screen scale, the far edge (z' ≈ -1532) at about
// 0.24x, exposing the "average density" weakness where the near end is undersampled.
constexpr int FloorSize = 2000;
constexpr int OutputWidth = 800;
constexpr int OutputHeight = 600;
constexpr float PerspectiveDepth = 500.0f;
constexpr float NearEdgeDepth = 400.0f;
constexpr float FloorRotationDegrees = -75.0f;
constexpr int GridStep = 8;
constexpr int BackgroundValue = 24;

static std::shared_ptr<Image> MakeFloorImage(Context* context) {
  auto surface = Surface::Make(context, FloorSize, FloorSize);
  if (surface == nullptr) {
    return nullptr;
  }
  auto* canvas = surface->getCanvas();
  canvas->clear(Color::FromRGBA(12, 12, 12, 255));

  Paint gridPaint;
  gridPaint.setColor(Color::FromRGBA(180, 180, 180, 255));
  gridPaint.setStrokeWidth(1.0f);
  for (int coordinate = 0; coordinate <= FloorSize; coordinate += GridStep) {
    canvas->drawLine(static_cast<float>(coordinate), 0, static_cast<float>(coordinate), FloorSize,
                     gridPaint);
    canvas->drawLine(0, static_cast<float>(coordinate), FloorSize, static_cast<float>(coordinate),
                     gridPaint);
  }

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  if (typeface == nullptr) {
    return nullptr;
  }
  Font font(typeface, 40.0f);
  auto label = TextBlob::MakeFrom("DENSITY 0123456789", font);
  if (label == nullptr) {
    return nullptr;
  }
  Paint textPaint;
  textPaint.setColor(Color::FromRGBA(255, 224, 64, 255));
  // Labels live in the source patch that maps to the output near edge so text is visible there.
  for (int y = 64; y < FloorSize; y += 160) {
    canvas->drawTextBlob(label, 1000, static_cast<float>(y), textPaint);
  }
  return surface->makeImageSnapshot();
}

static Matrix3D MakeFloorMatrix() {
  const float anchorX = static_cast<float>(FloorSize) * 0.5f;
  const auto offsetToAnchor = Matrix3D::MakeTranslate(-anchorX, 0, 0);
  const auto invOffsetToAnchor = Matrix3D::MakeTranslate(anchorX, 0, 0);
  // The near edge starts at z' = 400 (5x scale). Negative rotateX sends the far edge away from the
  // camera to roughly 0.24x scale.
  const auto modelMatrix = Matrix3D::MakeTranslate(0, 0, NearEdgeDepth) *
                           Matrix3D::MakeRotate({1, 0, 0}, FloorRotationDegrees);
  auto perspectiveMatrix = Matrix3D::I();
  perspectiveMatrix.setRowColumn(3, 2, -1.0f / PerspectiveDepth);
  // On the near edge, local x in [1000, 1040] maps to screen x in [200, 400] at 5x scale.
  const auto originTranslate = Matrix3D::MakeTranslate(-960, 60, 0);
  return originTranslate * invOffsetToAnchor * perspectiveMatrix * modelMatrix * offsetToAnchor;
}

static std::shared_ptr<Surface> RenderFloor(Context* context, const std::shared_ptr<Image>& image) {
  auto surface = Surface::Make(context, OutputWidth, OutputHeight);
  if (surface == nullptr) {
    return nullptr;
  }
  surface->getCanvas()->clear(
      Color::FromRGBA(BackgroundValue, BackgroundValue, BackgroundValue, 255));

  auto container = Layer::Make();
  container->setPreserve3D(true);
  auto floor = ImageLayer::Make();
  floor->setImage(image);
  floor->setMatrix3D(MakeFloorMatrix());
  container->addChild(floor);

  auto displayList = std::make_unique<DisplayList>();
  displayList->root()->addChild(container);
  displayList->render(surface.get());
  return surface;
}

}  // namespace

// A preserve-3D floor with strong perspective receding along one axis (see the review). The near
// edge is magnified about 5x while the far edge shrinks below 0.25x, exercising the average-density
// raster sizing path.
TGFX_TEST(Render3DDensityTradeoffTest, StrongPerspectiveFloor) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeFloorImage(context);
  ASSERT_TRUE(image != nullptr);

  auto surface = RenderFloor(context, image);
  ASSERT_TRUE(surface != nullptr);
  EXPECT_TRUE(Baseline::Compare(surface, "Render3DDensityTradeoffTest/StrongPerspectiveFloor"));
}

}  // namespace tgfx
