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

// The 2000x2000 source carries the dense 8px grid and text. The displayed plane is scaled to 20%
// and rotated so its upper/lower edges sit at z=10 and z=210, a strong but not extreme perspective
// that exposes the average-density raster sizing path.
constexpr int FloorSize = 2000;
constexpr int OutputWidth = 1200;
constexpr int OutputHeight = 1000;
constexpr float PerspectiveDepth = 500.0f;
constexpr float FloorScale = 0.2f;
constexpr float TopEdgeZ = 10.0f;
constexpr float BottomEdgeZ = 210.0f;
constexpr float FloorRotationDegrees = 30.0f;
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
  // Labels live in the source patch that maps to the output near edge so text stays visible.
  for (int y = 64; y < FloorSize; y += 160) {
    canvas->drawTextBlob(label, 1000, static_cast<float>(y), textPaint);
  }
  return surface->makeImageSnapshot();
}

static Matrix3D MakeFloorMatrix() {
  const float anchorX = static_cast<float>(FloorSize) * 0.5f;
  const auto offsetToAnchor = Matrix3D::MakeTranslate(-anchorX, 0, 0);
  const auto scale = Matrix3D::MakeScale(FloorScale, FloorScale, 1.0f);
  const auto rotation = Matrix3D::MakeRotate({1, 0, 0}, FloorRotationDegrees);
  const auto translateZ = Matrix3D::MakeTranslate(0, 0, TopEdgeZ);
  auto perspectiveMatrix = Matrix3D::I();
  perspectiveMatrix.setRowColumn(3, 2, -1.0f / PerspectiveDepth);
  // After scale+rotation, the upper and lower edges are z=10 and z=210 respectively. Their
  // projected y range lands in the center of the output surface.
  const auto originTranslate = Matrix3D::MakeTranslate(OutputWidth * 0.5f, 200, 0);
  return originTranslate * perspectiveMatrix * translateZ * rotation * scale * offsetToAnchor;
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

// A preserve-3D floor with a strong perspective spanning z=10 (upper edge) to z=210 (lower edge).
// The near/far density ratio is noticeable but the whole plane stays comfortably within GPU limits,
// exercising the average-density raster sizing path in a reproducible baseline.
TGFX_TEST(Render3DDensityTradeoffTest, PerspectiveFloor10To210) {
  // scale(0.2) followed by rotateX(30°) raises the 2000px lower edge by 200 in z.
  EXPECT_FLOAT_EQ(TopEdgeZ + FloorSize * FloorScale * 0.5f, BottomEdgeZ);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeFloorImage(context);
  ASSERT_TRUE(image != nullptr);

  auto surface = RenderFloor(context, image);
  ASSERT_TRUE(surface != nullptr);
  EXPECT_TRUE(Baseline::Compare(surface, "Render3DDensityTradeoffTest/PerspectiveFloor10To210"));
}

}  // namespace tgfx
