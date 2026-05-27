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

#include "tgfx/core/Image.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "tgfx/layers/layerstyles/NoiseStyle.h"
#include "tgfx/layers/vectors/ImagePattern.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(LayerStyleTest, NoiseStyleMovingRect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 600, 400);
  DisplayList displayList;
  displayList.setRenderMode(RenderMode::Tiled);
  displayList.setTileSize(64);

  auto parent = ShapeLayer::Make();
  parent->setFillStyle(ShapeStyle::Make(Color::White()));
  Path parentPath;
  parentPath.addRect(Rect::MakeXYWH(50.f, 50.f, 300.f, 300.f));
  parent->setPath(parentPath);
  displayList.root()->addChild(parent);

  auto child = ShapeLayer::Make();
  child->setFillStyle(ShapeStyle::Make(Color::White()));
  auto noise = NoiseStyle::MakeMono(4, 0.5f, Color{0.0f, 0.0f, 0.0f, 0.5f}, 6903);
  child->setLayerStyles({noise});
  parent->addChild(child);

  parent->setMatrix(Matrix::MakeTrans(0.f, 0.f));
  Path path;
  path.addRect(Rect::MakeXYWH(50.f, 100.f, 100.f, 100.f));
  child->setPath(path);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerStyleTest/NoiseStyleMovingRect"));
}

// Migrated from NoiseStyleTest.cpp
TGFX_TEST(LayerStyleTest, NoiseShiftText) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1000, 1000);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileSize(64);

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(1000);
  back->setHeight(1000);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  auto noise = NoiseStyle::MakeMono(10.0f, 0.5f, Color::FromRGBA(0, 0, 0, 128), 42.0f);

  auto textLayer = TextLayer::Make();
  textLayer->setText("style");
  textLayer->setFont(Font(typeface, 100.f));
  textLayer->setTextColor(Color::FromRGBA(192, 192, 192, 255));
  textLayer->setLayerStyles({noise});
  back->addChild(textLayer);
  displayList->root()->addChild(back);

  for (int i = 0; i < 8; i++) {
    surface->getCanvas()->clear();
    textLayer->setMatrix(Matrix::MakeTrans(-50.f + static_cast<float>(i) * 20.f, 20.f));
    displayList->render(surface.get());
  }

  auto textBounds = textLayer->getBounds();
  float centerX = (textBounds.left + textBounds.right) * 0.5f;
  float centerY = (textBounds.top + textBounds.bottom) * 0.5f;
  float lastX = -50.f + 7 * 20.f;
  float lastY = 20.f;

  surface->getCanvas()->clear();
  float angle = 20.f;
  auto matrix = Matrix::MakeTrans(lastX + centerX, lastY + centerY) * Matrix::MakeRotate(angle) *
                Matrix::MakeTrans(-centerX, -centerY);
  textLayer->setMatrix(matrix);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerStyleTest/NoiseShiftText"));
}

// Migrated from NoiseStyleTest.cpp
TGFX_TEST(LayerStyleTest, NoiseShapeText) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1000, 1000);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileSize(64);

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(1000);
  back->setHeight(1000);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  auto noise = NoiseStyle::MakeMono(10.0f, 0.5f, Color::FromRGBA(0, 0, 0, 128), 42.0f);

  auto shapeLayer = ShapeLayer::Make();
  Path path = {};
  path.addRect(Rect::MakeXYWH(50, 50, 400, 150));
  shapeLayer->setPath(path);

  auto textLayer = TextLayer::Make();
  textLayer->setText("style");
  textLayer->setFont(Font(typeface, 100.f));
  textLayer->setTextColor(Color::FromRGBA(192, 192, 192, 255));
  textLayer->setLayerStyles({noise});
  textLayer->setMatrix(Matrix::MakeTrans(50.f, 50.f));

  shapeLayer->addChild(textLayer);
  back->addChild(shapeLayer);
  displayList->root()->addChild(back);

  for (int i = 0; i < 8; i++) {
    surface->getCanvas()->clear();
    shapeLayer->setMatrix(Matrix::MakeTrans(static_cast<float>(i) * 20.f, 0.f));
    displayList->render(surface.get());
  }
  EXPECT_TRUE(Baseline::Compare(surface, "LayerStyleTest/NoiseShapeText"));
}

static std::shared_ptr<Image> MakeNoiseTileImage(int tileSize) {
  auto noiseShader = Shader::MakeFractalNoise(0.04f, 0.04f, 3, 42.0f);
  if (noiseShader == nullptr) {
    return nullptr;
  }
  PictureRecorder recorder = {};
  auto recordCanvas = recorder.beginRecording();
  Paint paint = {};
  paint.setShader(noiseShader);
  recordCanvas->drawRect(Rect::MakeWH(static_cast<float>(tileSize), static_cast<float>(tileSize)),
                         paint);
  auto picture = recorder.finishRecordingAsPicture();
  if (picture == nullptr) {
    return nullptr;
  }
  return Image::MakeFrom(std::move(picture), tileSize, tileSize);
}

// Migrated from NoiseStyleTest.cpp
TGFX_TEST(LayerStyleTest, NoiseMovingRect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1000, 1000);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(1000);
  back->setHeight(1000);

  auto parent = ShapeLayer::Make();
  parent->setFillStyle(ShapeStyle::Make(Color::White()));
  Path parentPath;
  parentPath.addRect(Rect::MakeXYWH(50.f, 50.f, 900.f, 900.f));
  parent->setPath(parentPath);

  auto child = ShapeLayer::Make();
  child->setFillStyle(ShapeStyle::Make(Color::White()));
  auto noise = NoiseStyle::MakeMono(10.0f, 0.505f, Color::FromRGBA(0, 0, 0, 128), 42.0f);
  child->setLayerStyles({noise});
  parent->addChild(child);

  back->addChild(parent);
  displayList->root()->addChild(back);

  Path childPath;
  childPath.addRect(Rect::MakeXYWH(-150.f, 50.f, 200.f, 200.f));
  child->setPath(childPath);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerStyleTest/NoiseMovingRect"));
}

// Migrated from NoiseStyleTest.cpp
TGFX_TEST(LayerStyleTest, NoiseMovingRectTiled) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1000, 1000);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileSize(64);

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(1000);
  back->setHeight(1000);

  auto parent = ShapeLayer::Make();
  parent->setFillStyle(ShapeStyle::Make(Color::White()));
  Path parentPath;
  parentPath.addRect(Rect::MakeXYWH(50.f, 50.f, 900.f, 900.f));
  parent->setPath(parentPath);

  auto child = ShapeLayer::Make();
  child->setFillStyle(ShapeStyle::Make(Color::White()));
  auto noise = NoiseStyle::MakeMono(10.0f, 0.505f, Color::FromRGBA(0, 0, 0, 128), 42.0f);
  child->setLayerStyles({noise});
  parent->addChild(child);

  back->addChild(parent);
  displayList->root()->addChild(back);

  for (int i = 0; i < 16; i++) {
    surface->getCanvas()->clear();
    Path childPath;
    auto offsetX = -150.f + static_cast<float>(i) * 20.f;
    childPath.addRect(Rect::MakeXYWH(offsetX, 50.f, 200.f, 200.f));
    child->setPath(childPath);
    displayList->render(surface.get());
  }
  EXPECT_TRUE(Baseline::Compare(surface, "LayerStyleTest/NoiseMovingRectTiled"));
}

// Migrated from NoiseStyleTest.cpp
TGFX_TEST(LayerStyleTest, NoiseTileMode) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1000, 1000);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(1000);
  back->setHeight(1000);
  displayList->root()->addChild(back);

  auto tileImage = MakeNoiseTileImage(100);
  ASSERT_TRUE(tileImage != nullptr);

  auto tileModes = {TileMode::Clamp, TileMode::Repeat, TileMode::Mirror, TileMode::Decal};
  int col = 0;
  for (auto mode : tileModes) {
    auto pattern = ImagePattern::Make(tileImage, mode, mode);
    pattern->setScaleMode(ScaleMode::None);
    pattern->setMatrix(Matrix::MakeScale(0.5f));

    auto imageShader = pattern->getShader();
    auto shapeStyle = ShapeStyle::Make(imageShader);

    auto shapeLayer = ShapeLayer::Make();
    Path path = {};
    path.addRect(Rect::MakeXYWH(0, 0, 200, 200));
    shapeLayer->setPath(path);
    shapeLayer->setFillStyle(shapeStyle);
    shapeLayer->setMatrix(Matrix::MakeTrans(50.f + static_cast<float>(col) * 230.f, 50.f));

    back->addChild(shapeLayer);
    col++;
  }

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerStyleTest/NoiseTileMode"));
}

// Migrated from NoiseStyleTest.cpp
TGFX_TEST(LayerStyleTest, NoiseMovingRectWithDropShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1000, 1000);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(1000);
  back->setHeight(1000);

  auto parent = ShapeLayer::Make();
  parent->setFillStyle(ShapeStyle::Make(Color::White()));
  Path parentPath;
  parentPath.addRect(Rect::MakeXYWH(50.f, 50.f, 900.f, 900.f));
  parent->setPath(parentPath);

  auto child = ShapeLayer::Make();
  child->setFillStyle(ShapeStyle::Make(Color::White()));
  auto noise = NoiseStyle::MakeMono(10.0f, 0.505f, Color::FromRGBA(0, 0, 0, 128), 42.0f);
  auto shadow = DropShadowStyle::Make(10.f, 10.f, 5.f, 5.f, Color::FromRGBA(0, 0, 0, 128), false);
  child->setLayerStyles({noise, shadow});
  parent->addChild(child);

  back->addChild(parent);
  displayList->root()->addChild(back);

  for (int i = 0; i < 16; i++) {
    surface->getCanvas()->clear();
    Path childPath;
    auto offsetX = -150.f + static_cast<float>(i) * 20.f;
    childPath.addRect(Rect::MakeXYWH(offsetX, 50.f, 200.f, 200.f));
    child->setPath(childPath);
    displayList->render(surface.get());
  }
  EXPECT_TRUE(Baseline::Compare(surface, "LayerStyleTest/NoiseMovingRectWithDropShadow"));
}

enum class NoiseStyleSweepType { Mono, Duo, Multi };

static std::shared_ptr<NoiseStyle> MakeDensitySweepStyle(NoiseStyleSweepType type, float density) {
  switch (type) {
    case NoiseStyleSweepType::Mono:
      return NoiseStyle::MakeMono(25.0f, density, Color::FromRGBA(255, 0, 0, 128), 42.0f);
    case NoiseStyleSweepType::Duo:
      return NoiseStyle::MakeDuo(25.0f, density, Color::FromRGBA(255, 0, 0, 128),
                                 Color::FromRGBA(0, 0, 255, 128), 42.0f);
    case NoiseStyleSweepType::Multi:
      return NoiseStyle::MakeMulti(25.0f, density, 1.0f, 42.0f);
  }
  return nullptr;
}

static void RenderNoiseStyleDensitySweep(Context* context, NoiseStyleSweepType type,
                                         const std::string& keyPrefix) {
  constexpr float size = 1000.0f;
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 300.0f);
  auto density = 0.5f;
  auto surface = Surface::Make(context, static_cast<int>(size), static_cast<int>(size));
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(size);
  back->setHeight(size);

  auto layer = TextLayer::Make();
  layer->setText("text");
  layer->setFont(font);
  layer->setTextColor(Color::FromRGBA(60, 120, 200));
  auto textBounds = layer->getBounds(nullptr, true);
  layer->setMatrix(
      Matrix::MakeTrans(size * 0.5f - textBounds.centerX(), size * 0.5f - textBounds.centerY()));

  auto noise = MakeDensitySweepStyle(type, density);
  ASSERT_TRUE(noise != nullptr);
  layer->setLayerStyles({noise});

  back->addChild(layer);
  displayList->root()->addChild(back);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, keyPrefix));
}

TGFX_TEST(LayerStyleTest, MonoNoiseStyleDensitySweep) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderNoiseStyleDensitySweep(context, NoiseStyleSweepType::Mono,
                               "LayerStyleTest/MonoNoiseStyleDensitySweep");
}

TGFX_TEST(LayerStyleTest, DuoNoiseStyleDensitySweep) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderNoiseStyleDensitySweep(context, NoiseStyleSweepType::Duo,
                               "LayerStyleTest/DuoNoiseStyleDensitySweep");
}

TGFX_TEST(LayerStyleTest, MultiNoiseStyleDensitySweep) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderNoiseStyleDensitySweep(context, NoiseStyleSweepType::Multi,
                               "LayerStyleTest/MultiNoiseStyleDensitySweep");
}

// Verify blend mode with MonoNoiseStyle.
TGFX_TEST(LayerStyleTest, NoiseStyleBlendMode) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto rectSize = 200.0f;
  auto padding = 50.0f;
  auto surfaceSize = static_cast<int>(rectSize + padding * 2.0f);
  auto surface = Surface::Make(context, surfaceSize, surfaceSize);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(static_cast<float>(surfaceSize));
  back->setHeight(static_cast<float>(surfaceSize));
  displayList->root()->addChild(back);

  auto layer = ShapeLayer::Make();
  layer->setMatrix(Matrix::MakeTrans(padding, padding));
  Path path;
  path.addRoundRect(Rect::MakeWH(rectSize, rectSize), 20, 20);
  layer->setPath(path);
  layer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(60, 120, 200)));
  auto style = NoiseStyle::MakeMono(6.0f, 1.0f, Color::FromRGBA(0, 0, 0, 128), 42.0f);
  ASSERT_TRUE(style != nullptr);
  style->setBlendMode(BlendMode::SrcOver);
  layer->setLayerStyles({style});
  back->addChild(layer);

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerStyleTest/NoiseStyleBlendMode_Mono_SrcOver"));
}

}  // namespace tgfx
