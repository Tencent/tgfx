/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "core/filters/GaussianBlurImageFilter.h"
#include "core/utils/Log.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/filters/BlendFilter.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/filters/ColorMatrixFilter.h"
#include "tgfx/layers/filters/DropShadowFilter.h"
#include "tgfx/layers/filters/InnerShadowFilter.h"
#include "tgfx/layers/filters/NoiseFilter.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "tgfx/layers/layerstyles/InnerShadowStyle.h"
#include "tgfx/layers/layerstyles/NoiseStyle.h"
#include "tgfx/layers/vectors/ImagePattern.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(LayerFilterTest, FilterTest) {
  auto filter = DropShadowFilter::Make(-80, -80, 0, 0, Color::Black());
  auto filter2 = DropShadowFilter::Make(-40, -40, 0, 0, Color::Green());
  auto filter3 = BlurFilter::Make(10, 10);
  auto image = MakeImage("resources/apitest/rotation.jpg");
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, image->width(), image->height());
  auto displayList = std::make_unique<DisplayList>();
  auto layer = ImageLayer::Make();
  layer->setImage(image);
  auto matrix = Matrix::MakeScale(0.5f);
  matrix.postTranslate(200, 200);
  layer->setMatrix(matrix);
  layer->setFilters({filter3, filter, filter2});
  displayList->root()->addChild(layer);
  displayList->render(surface.get());
  auto bounds = displayList->root()->getBounds();
  EXPECT_EQ(Rect::MakeLTRB(130.f, 130.f, 1722.f, 2226.f), bounds);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/filterTest"));
}

TGFX_TEST(LayerFilterTest, filterClip) {
  auto filter = DropShadowFilter::Make(-10, -10, 0, 0, Color::Black());

  auto image = MakeImage("resources/apitest/rotation.jpg");
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto displayList = std::make_unique<DisplayList>();
  auto layer = ImageLayer::Make();
  layer->setImage(image);
  auto matrix = Matrix::MakeScale(0.5f);
  matrix.postTranslate(50, 50);
  layer->setMatrix(matrix);
  layer->setFilters({filter});
  displayList->root()->addChild(layer);
  displayList->render(surface.get());
  auto bounds = displayList->root()->getBounds();
  EXPECT_EQ(Rect::MakeLTRB(45.f, 45.f, 1562.f, 2066.f), bounds);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/filterClip"));
}

TGFX_TEST(LayerFilterTest, dropshadowLayerFilter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/image_as_mask.png");
  ASSERT_TRUE(image != nullptr);
  auto imageWidth = static_cast<float>(image->width());
  auto imageHeight = static_cast<float>(image->height());
  auto padding = 30.f;
  Paint paint;
  auto surface = Surface::Make(context, static_cast<int>(imageWidth * 2.f + padding * 3.f),
                               static_cast<int>(imageHeight * 2.f + padding * 3.f));
  auto filter = BlurFilter::Make(5, 5);
  auto layer = ImageLayer::Make();
  layer->setImage(image);
  layer->setMatrix(Matrix::MakeTrans(padding, padding));
  layer->setFilters({filter});
  auto displayList = std::make_unique<DisplayList>();
  displayList->root()->addChild(layer);

  auto layer2 = ImageLayer::Make();
  layer2->setImage(image);
  layer2->setMatrix(Matrix::MakeTrans(imageWidth + padding * 2, padding));
  auto filter2 = DropShadowFilter::Make(0, 0, 5, 5, Color::White(), true);
  layer2->setFilters({filter2});
  displayList->root()->addChild(layer2);

  auto layer3 = ImageLayer::Make();
  layer3->setImage(image);
  layer3->setMatrix(Matrix::MakeTrans(padding, imageWidth + padding * 2));
  auto filter3 = DropShadowFilter::Make(0, 0, 5, 5, Color::White());
  layer3->setFilters({filter3});
  displayList->root()->addChild(layer3);

  auto layer4 = ImageLayer::Make();
  layer4->setImage(image);
  layer4->setMatrix(Matrix::MakeTrans(imageWidth + padding * 2, imageWidth + padding * 2));
  auto filter4 = DropShadowFilter::Make(3, 3, 0, 0, Color::White());
  layer4->setFilters({filter4});
  displayList->root()->addChild(layer4);

  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/dropShadow"));

  auto src = Rect::MakeXYWH(10, 10, 10, 10);
  auto bounds = filter4->filterBounds(src, 1.0f, MapDirection::Forward);
  EXPECT_EQ(bounds, Rect::MakeXYWH(10, 10, 13, 13));
  bounds = ImageFilter::DropShadowOnly(3, 3, 0, 0, Color::White())->filterBounds(src);
  EXPECT_EQ(bounds, Rect::MakeXYWH(13, 13, 10, 10));
}

TGFX_TEST(LayerFilterTest, colorBlendLayerFilter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/rotation.jpg");
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, image->width() / 4, image->height() / 4);

  auto filter = BlendFilter::Make(Color::Red(), BlendMode::Multiply);

  auto displayList = std::make_unique<DisplayList>();
  auto layer = ImageLayer::Make();
  layer->setImage(image);
  layer->setFilters({filter});
  displayList->root()->addChild(layer);
  layer->setMatrix(Matrix::MakeScale(0.25f));
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/ModeColorFilter"));
}

TGFX_TEST(LayerFilterTest, colorMatrixLayerFilter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, image->width(), image->height());
  Paint paint;
  std::array<float, 20> matrix = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
  auto displayList = std::make_unique<DisplayList>();
  auto layer = ImageLayer::Make();
  layer->setImage(image);
  auto filter = ColorMatrixFilter::Make(matrix);
  layer->setFilters({filter});
  displayList->root()->addChild(layer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/identityMatrix"));
  std::array<float, 20> greyColorMatrix = {0.21f, 0.72f, 0.07f, 0.41f, 0,  // red
                                           0.21f, 0.72f, 0.07f, 0.41f, 0,  // green
                                           0.21f, 0.72f, 0.07f, 0.41f, 0,  // blue
                                           0,     0,     0,     1.0f,  0};

  filter->setMatrix(greyColorMatrix);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/greyColorMatrix"));
}

TGFX_TEST(LayerFilterTest, blurLayerFilter) {
  auto blur = BlurFilter::Make(130.f, 130.f);
  EXPECT_EQ(blur->blurrinessY(), 130.f);
  EXPECT_EQ(blur->blurrinessX(), 130.f);
  blur->setTileMode(TileMode::Clamp);
  EXPECT_EQ(blur->tileMode(), TileMode::Clamp);
  auto imageFilter = std::static_pointer_cast<GaussianBlurImageFilter>(blur->getImageFilter(0.5f));
  auto imageFilter2 = std::static_pointer_cast<GaussianBlurImageFilter>(
      ImageFilter::Blur(65.f, 65.f, TileMode::Clamp));
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(imageFilter->blurrinessX, imageFilter2->blurrinessX);
                      EXPECT_EQ(imageFilter->blurrinessY, imageFilter2->blurrinessY);
                      EXPECT_EQ(imageFilter->tileMode, imageFilter2->tileMode);)

  EXPECT_EQ(blur->getImageFilter(0.5f)->filterBounds(Rect::MakeWH(200, 200)),
            imageFilter2->filterBounds(Rect::MakeWH(200, 200)));
}

TGFX_TEST(LayerFilterTest, InnerShadowFilter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  auto imageWidth = static_cast<float>(image->width());
  auto imageHeight = static_cast<float>(image->height());
  auto padding = 30.f;
  Paint paint;
  auto surface = Surface::Make(context, static_cast<int>(imageWidth * 2.f + padding * 3.f),
                               static_cast<int>(imageHeight * 2.f + padding * 3.f));
  auto filter = BlurFilter::Make(15, 15);
  auto layer = ImageLayer::Make();
  layer->setImage(image);
  layer->setMatrix(Matrix::MakeTrans(padding, padding));
  layer->setFilters({filter});
  auto displayList = std::make_unique<DisplayList>();
  displayList->root()->addChild(layer);

  auto layer2 = ImageLayer::Make();
  layer2->setImage(image);
  layer2->setMatrix(Matrix::MakeTrans(imageWidth + padding * 2, padding));
  auto filter2 = InnerShadowFilter::Make(0, 0, 15, 15, Color::Black(), true);
  layer2->setFilters({filter2});
  displayList->root()->addChild(layer2);

  auto layer3 = ImageLayer::Make();
  layer3->setImage(image);
  layer3->setMatrix(Matrix::MakeTrans(padding, imageWidth + padding * 2));
  auto filter3 = InnerShadowFilter::Make(0, 0, 15, 15, Color::Black());
  layer3->setFilters({filter3});
  displayList->root()->addChild(layer3);

  auto layer4 = ImageLayer::Make();
  layer4->setImage(image);
  layer4->setMatrix(Matrix::MakeTrans(imageWidth + padding * 2, imageWidth + padding * 2));
  auto filter4 = InnerShadowFilter::Make(1, 1, 0, 0, Color::Black());
  layer4->setFilters({filter4});
  displayList->root()->addChild(layer4);

  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/innerShadow"));
}

TGFX_TEST(LayerFilterTest, DropShadowStyle) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 150, 150);
  auto displayList = std::make_unique<DisplayList>();
  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(150);
  back->setHeight(150);
  auto layer = ShapeLayer::Make();
  layer->setMatrix(Matrix::MakeTrans(30, 30));
  Path path;
  path.addRect(Rect::MakeWH(100, 100));
  layer->setPath(path);
  auto fillStyle = ShapeStyle::Make(Color::FromRGBA(100, 0, 0, 128));
  layer->setFillStyle(fillStyle);
  layer->setLineWidth(2.0f);
  layer->setBlendMode(BlendMode::Lighten);

  auto shadowLayer = Layer::Make();
  auto style = DropShadowStyle::Make(10, 10, 0, 0, Color::Black(), false);
  style->setExcludeChildEffects(true);
  shadowLayer->setLayerStyles({style});
  shadowLayer->addChild(layer);
  back->addChild(shadowLayer);
  displayList->root()->addChild(back);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/DropShadowStyle"));

  style->setBlendMode(BlendMode::Luminosity);
  style->setOffsetX(0);
  style->setOffsetY(-20);
  style->setShowBehindLayer(true);
  shadowLayer->setAlpha(0.5);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/DropShadowStyle2"));

  layer->setBlendMode(BlendMode::Multiply);
  layer->setFillStyle(nullptr);
  layer->setStrokeStyle(ShapeStyle::Make(Color::FromRGBA(100, 0, 0, 128)));
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/DropShadowStyle-stroke-behindLayer"));

  style->setShowBehindLayer(false);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/DropShadowStyle-stroke"));

  auto blur = BlurFilter::Make(2.5, 2.5);
  layer->setFilters({blur});
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/DropShadowStyle-stroke-blur"));

  style->setShowBehindLayer(true);
  displayList->render(surface.get());
  EXPECT_TRUE(
      Baseline::Compare(surface, "LayerFilterTest/DropShadowStyle-stroke-blur-behindLayer"));
}

TGFX_TEST(LayerFilterTest, InnerShadowStyle) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 150, 150);
  auto displayList = std::make_unique<DisplayList>();
  auto layer = ShapeLayer::Make();
  layer->setMatrix(Matrix::MakeTrans(30, 30));
  Path path;
  path.addRect(Rect::MakeWH(100, 100));
  Path path2;
  path2.addRect(Rect::MakeWH(50, 50));
  path2.transform(Matrix::MakeTrans(20, 20));
  path.addPath(path2, PathOp::Difference);
  layer->setPath(path);
  auto fillStyle = ShapeStyle::Make(Color::FromRGBA(100, 0, 0, 128));
  layer->setFillStyle(fillStyle);
  auto style = InnerShadowStyle::Make(10, 10, 0, 0, Color::Black());
  layer->setLayerStyles({style});
  displayList->root()->addChild(layer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/InnerShadowStyle"));
}

TGFX_TEST(LayerFilterTest, Filters) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 150, 150);
  auto displayList = std::make_unique<DisplayList>();
  auto layer = ShapeLayer::Make();
  layer->setMatrix(Matrix::MakeTrans(30, 30));
  Path path;
  path.addRect(Rect::MakeWH(100, 100));
  layer->setPath(path);
  auto fillStyle = ShapeStyle::Make(Color::FromRGBA(100, 0, 0, 128));
  layer->setFillStyle(fillStyle);
  auto filter = BlurFilter::Make(5, 5);
  auto filter2 = DropShadowFilter::Make(10, 10, 0, 0, Color::Black());
  auto filter3 = InnerShadowFilter::Make(10, 10, 0, 0, Color::White());
  layer->setFilters({filter, filter2, filter3});
  displayList->root()->addChild(layer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/filters"));
}

TGFX_TEST(LayerFilterTest, PartialInnerShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList displayList;
  auto surface = Surface::Make(context, 100, 100);
  auto rootLayer = Layer::Make();
  displayList.root()->addChild(rootLayer);
  auto shapeLayer = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeXYWH(0, 0, 100, 100));
  shapeLayer->setPath(path);
  shapeLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 255, 255, 255)));
  shapeLayer->setLineWidth(1.0f);
  rootLayer->addChild(shapeLayer);

  auto innerShadowStyle = InnerShadowStyle::Make(10, 10, 0, 0, Color::Black());
  displayList.setContentOffset(-5, -5);
  displayList.setRenderMode(RenderMode::Tiled);
  displayList.render(surface.get());

  shapeLayer->setLayerStyles({});
  shapeLayer->setLayerStyles({innerShadowStyle});
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/PartialInnerShadow"));
}

TGFX_TEST(LayerFilterTest, DropShadowDirtyRect) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList displayList;
  auto surface = Surface::Make(context, 200, 200);
  auto rootLayer = Layer::Make();
  displayList.root()->addChild(rootLayer);
  auto shapeLayer = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeXYWH(0, 0, 100, 100));
  shapeLayer->setPath(path);
  shapeLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 0, 0, 255)));
  shapeLayer->setLayerStyles({DropShadowStyle::Make(10, 10, 0, 0, Color::Black())});
  rootLayer->addChild(shapeLayer);
  shapeLayer->setMatrix(Matrix::MakeRotate(-120));
  displayList.setContentOffset(50, 150);
  displayList.render(surface.get());
  shapeLayer->setMatrix(Matrix::MakeRotate(-121));
  displayList.showDirtyRegions(true);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/DropShadowDirtyRect"));
}

TGFX_TEST(LayerFilterTest, ShapeLayerContourWithDropShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(200);
  back->setHeight(200);
  displayList->root()->addChild(back);

  // Parent layer with rect fill and drop shadow
  auto parent = ShapeLayer::Make();
  parent->setMatrix(Matrix::MakeTrans(50, 50));
  Path parentPath;
  parentPath.addRect(Rect::MakeWH(100, 100));
  parent->setPath(parentPath);
  parent->setFillStyle(ShapeStyle::Make(Color::Blue()));
  auto dropShadow = DropShadowStyle::Make(8, 8, 5, 5, Color::Black(), false);
  parent->setLayerStyles({dropShadow});

  // Child ShapeLayer with only stroke style (no fill style)
  // This tests that the contour-only content is correctly generated for layer styles.
  auto child = ShapeLayer::Make();
  child->setMatrix(Matrix::MakeTrans(30, 30));
  Path childPath;
  childPath.addRoundRect(Rect::MakeWH(80, 80), 15, 15);
  child->setPath(childPath);
  child->setStrokeStyle(ShapeStyle::Make(Color::Red()));
  child->setLineWidth(4.0f);

  parent->addChild(child);
  back->addChild(parent);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/ShapeLayerContourWithDropShadow"));

  // Test with no fill and no stroke - child should be transparent, shadow only shows parent
  child->removeStrokeStyles();
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/ShapeLayerNoStyleWithDropShadow"));
}

TGFX_TEST(LayerFilterTest, ScaledRectWithInnerShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  // Rect 100x100 with InnerShadow, scale 14.4x, surface 270x270
  auto surface = Surface::Make(context, 270, 270);
  auto displayList = std::make_unique<DisplayList>();

  auto shapeLayer = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeWH(100, 100));
  shapeLayer->setPath(path);
  shapeLayer->setFillStyle(ShapeStyle::Make(Color::White()));

  // InnerShadow: offsetX=0, offsetY=-2, blurrinessX=0, blurrinessY=0
  shapeLayer->setAllowsEdgeAntialiasing(true);
  auto innerShadow = InnerShadowStyle::Make(0, -2, 0, 0, Color::FromRGBA(0, 0, 0, 128));
  shapeLayer->setLayerStyles({innerShadow});

  displayList->setContentOffset(-1200, -1300);
  displayList->setZoomScale(14.4125204f);
  displayList->setBackgroundColor(Color::White());
  displayList->root()->addChild(shapeLayer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/ScaledRectWithInnerShadow"));
}

enum class NoiseMode { Mono, Duo, Multi };

// Builds a NoiseFilter with the given mode using a fixed cosmetic palette so the
// Filter and Style baselines can be visually compared.
static std::shared_ptr<LayerFilter> MakeNoiseFilter(NoiseMode mode, float size, float density,
                                                    BlendMode blendMode = BlendMode::SrcOver) {
  switch (mode) {
    case NoiseMode::Mono:
      return NoiseFilter::MakeMono(size, density, Color::FromRGBA(255, 0, 0, 128), 42.0f,
                                   blendMode);
    case NoiseMode::Duo:
      return NoiseFilter::MakeDuo(size, density, Color::FromRGBA(255, 0, 0, 128),
                                  Color::FromRGBA(0, 0, 255, 128), 42.0f, blendMode);
    case NoiseMode::Multi:
      return NoiseFilter::MakeMulti(size, density, 1.0f, 42.0f, blendMode);
  }
  return nullptr;
}

static std::shared_ptr<NoiseStyle> MakeNoiseStyle(NoiseMode mode, float size, float density) {
  switch (mode) {
    case NoiseMode::Mono:
      return NoiseStyle::MakeMono(size, density, Color::FromRGBA(255, 0, 0, 128), 42.0f);
    case NoiseMode::Duo:
      return NoiseStyle::MakeDuo(size, density, Color::FromRGBA(255, 0, 0, 128),
                                 Color::FromRGBA(0, 0, 255, 128), 42.0f);
    case NoiseMode::Multi:
      return NoiseStyle::MakeMulti(size, density, 1.0f, 42.0f);
  }
  return nullptr;
}

static std::shared_ptr<ShapeLayer> MakeFilledRect(const Rect& rect, const Color& color) {
  auto layer = ShapeLayer::Make();
  Path path;
  path.addRect(rect);
  layer->setPath(path);
  layer->setFillStyle(ShapeStyle::Make(color));
  return layer;
}

// 3 (Mono/Duo/Multi) x 2 (Filter/Style) grid. Each cell renders a colored rect with the
// corresponding noise applied. Covers the basic pixel output of all six combinations in one
// baseline.
TGFX_TEST(LayerFilterTest, BasicMonoDuoMulti) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  constexpr float CellW = 200.0f;
  constexpr float CellH = 200.0f;
  constexpr float Padding = 20.0f;
  constexpr int Cols = 3;
  constexpr int Rows = 2;
  auto width = static_cast<int>(Padding * (Cols + 1) + CellW * Cols);
  auto height = static_cast<int>(Padding * (Rows + 1) + CellH * Rows);
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(static_cast<float>(width));
  back->setHeight(static_cast<float>(height));
  displayList->root()->addChild(back);

  NoiseMode modes[Cols] = {NoiseMode::Mono, NoiseMode::Duo, NoiseMode::Multi};
  auto fill = Color::FromRGBA(60, 120, 200);
  for (int row = 0; row < Rows; ++row) {
    bool useFilter = (row == 0);
    for (int col = 0; col < Cols; ++col) {
      auto cell = MakeFilledRect(Rect::MakeWH(CellW, CellH), fill);
      cell->setMatrix(Matrix::MakeTrans(Padding + static_cast<float>(col) * (CellW + Padding),
                                        Padding + static_cast<float>(row) * (CellH + Padding)));
      if (useFilter) {
        cell->setFilters({MakeNoiseFilter(modes[col], 6.0f, 0.5f)});
      } else {
        cell->setLayerStyles({MakeNoiseStyle(modes[col], 6.0f, 0.5f)});
      }
      back->addChild(cell);
    }
  }

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/BasicMonoDuoMulti"));
}

// 6 rows (Mono/Duo/Multi x Filter/Style) x N density columns. Verifies the density transition
// is monotonic and produces a stable visual sweep for all modes / both consumers.
TGFX_TEST(LayerFilterTest, DensitySweep) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  constexpr float CellW = 160.0f;
  constexpr float CellH = 160.0f;
  constexpr float Padding = 20.0f;
  constexpr int Cols = 5;  // density values
  constexpr int Rows = 6;  // 3 modes x 2 (filter, style)
  const float densities[Cols] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
  auto width = static_cast<int>(Padding * (Cols + 1) + CellW * Cols);
  auto height = static_cast<int>(Padding * (Rows + 1) + CellH * Rows);
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(static_cast<float>(width));
  back->setHeight(static_cast<float>(height));
  displayList->root()->addChild(back);

  NoiseMode modes[3] = {NoiseMode::Mono, NoiseMode::Duo, NoiseMode::Multi};
  auto fill = Color::FromRGBA(60, 120, 200);
  for (int row = 0; row < Rows; ++row) {
    int modeIndex = row / 2;
    bool useFilter = (row % 2) == 0;
    for (int col = 0; col < Cols; ++col) {
      auto cell = MakeFilledRect(Rect::MakeWH(CellW, CellH), fill);
      cell->setMatrix(Matrix::MakeTrans(Padding + static_cast<float>(col) * (CellW + Padding),
                                        Padding + static_cast<float>(row) * (CellH + Padding)));
      if (useFilter) {
        cell->setFilters({MakeNoiseFilter(modes[modeIndex], 12.0f, densities[col])});
      } else {
        cell->setLayerStyles({MakeNoiseStyle(modes[modeIndex], 12.0f, densities[col])});
      }
      back->addChild(cell);
    }
  }

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/DensitySweep"));
}

// Covers blend mode parity between NoiseFilter and NoiseStyle. Two rows: top = Filter,
// bottom = Style; columns iterate through SrcOver / Multiply / Screen / Overlay.
TGFX_TEST(LayerFilterTest, BlendMode) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  constexpr float CellW = 180.0f;
  constexpr float CellH = 180.0f;
  constexpr float Padding = 20.0f;
  constexpr int Cols = 4;
  constexpr int Rows = 2;
  const BlendMode modes[Cols] = {BlendMode::SrcOver, BlendMode::Multiply, BlendMode::Screen,
                                 BlendMode::Overlay};
  auto width = static_cast<int>(Padding * (Cols + 1) + CellW * Cols);
  auto height = static_cast<int>(Padding * (Rows + 1) + CellH * Rows);
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(static_cast<float>(width));
  back->setHeight(static_cast<float>(height));
  displayList->root()->addChild(back);

  auto fill = Color::FromRGBA(100, 150, 200);
  for (int col = 0; col < Cols; ++col) {
    {
      auto cell = MakeFilledRect(Rect::MakeWH(CellW, CellH), fill);
      cell->setMatrix(
          Matrix::MakeTrans(Padding + static_cast<float>(col) * (CellW + Padding), Padding));
      cell->setFilters({MakeNoiseFilter(NoiseMode::Mono, 6.0f, 1.0f, modes[col])});
      back->addChild(cell);
    }
    {
      auto cell = MakeFilledRect(Rect::MakeWH(CellW, CellH), fill);
      cell->setMatrix(Matrix::MakeTrans(Padding + static_cast<float>(col) * (CellW + Padding),
                                        Padding * 2.0f + CellH));
      auto style = MakeNoiseStyle(NoiseMode::Mono, 6.0f, 1.0f);
      style->setBlendMode(modes[col]);
      cell->setLayerStyles({style});
      back->addChild(cell);
    }
  }

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/BlendMode"));
}

// Renders the layer at two positions back-to-back under tiled rendering. The first frame
// populates the tile cache; the second frame exercises tile reuse with shifted geometry. If the
// noise pattern were anchored to the input image origin (or to a stale tile-cache origin) rather
// than to the layer geometry, the second frame would show seams or drift in the cached tiles.
// The baseline captures the second frame only.
TGFX_TEST(LayerFilterTest, AnchorStability) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  constexpr int Width = 600;
  constexpr int Height = 300;
  auto surface = Surface::Make(context, Width, Height);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileSize(64);

  auto back = Layer::Make();
  displayList->root()->addChild(back);

  // Top half: layer with NoiseFilter.
  auto filterLayer = MakeFilledRect(Rect::MakeWH(150.0f, 100.0f), Color::FromRGBA(60, 120, 200));
  filterLayer->setFilters({MakeNoiseFilter(NoiseMode::Mono, 8.0f, 0.5f)});
  back->addChild(filterLayer);

  // Bottom half: layer with NoiseStyle.
  auto styleLayer = MakeFilledRect(Rect::MakeWH(150.0f, 100.0f), Color::FromRGBA(60, 120, 200));
  styleLayer->setLayerStyles({MakeNoiseStyle(NoiseMode::Mono, 8.0f, 0.5f)});
  back->addChild(styleLayer);

  filterLayer->setMatrix(Matrix::MakeTrans(0, 30.0f));
  styleLayer->setMatrix(Matrix::MakeTrans(0, 170.0f));
  const float positions[2] = {30.0f, 230.0f};
  for (auto offsetX : positions) {
    filterLayer->setMatrix(Matrix::MakeTrans(offsetX, 30.0f));
    styleLayer->setMatrix(Matrix::MakeTrans(offsetX, 170.0f));
    displayList->render(surface.get(), false);
    context->flush();
  }

  // diff anchor
  auto path = filterLayer->path();
  path.transform(Matrix::MakeTrans(200.f, 0));
  filterLayer->setPath(path);

  path = styleLayer->path();
  path.transform(Matrix::MakeTrans(200.f, 0));
  styleLayer->setPath(path);

  displayList->render(surface.get(), false);
  context->flush();

  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/AnchorStability"));
}

// Verifies noise correctly composes with a preceding BlurFilter and a sibling DropShadowStyle.
// Top row: NoiseFilter chained after Blur, with a DropShadowStyle. Bottom row: NoiseStyle
// composed on top of the same DropShadowStyle. Confirms the filter chain order and layer style
// stacking remain visually correct after the noise feature lands.
TGFX_TEST(LayerFilterTest, WithBlurAndShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  constexpr int Width = 600;
  constexpr int Height = 300;
  auto surface = Surface::Make(context, Width, Height);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(static_cast<float>(Width));
  back->setHeight(static_cast<float>(Height));
  displayList->root()->addChild(back);

  auto fill = Color::FromRGBA(60, 120, 200);

  // Filter side: Blur -> NoiseFilter chain, with DropShadow style.
  {
    auto layer = MakeFilledRect(Rect::MakeWH(180.0f, 180.0f), fill);
    layer->setMatrix(Matrix::MakeTrans(60.0f, 60.0f));
    auto blur = BlurFilter::Make(3.0f, 3.0f);
    auto noise = NoiseFilter::MakeMono(6.0f, 0.5f, Color::FromRGBA(0, 0, 0, 128), 42.0f,
                                       BlendMode::Multiply);
    layer->setFilters({blur, noise});
    auto shadow = DropShadowStyle::Make(8.0f, 8.0f, 5.0f, 5.0f, Color::Black());
    layer->setLayerStyles({shadow});
    back->addChild(layer);
  }

  // Style side: NoiseStyle stacked with DropShadow style.
  {
    auto layer = MakeFilledRect(Rect::MakeWH(180.0f, 180.0f), fill);
    layer->setMatrix(Matrix::MakeTrans(360.0f, 60.0f));
    auto noise = NoiseStyle::MakeMono(6.0f, 0.5f, Color::FromRGBA(0, 0, 0, 128), 42.0f);
    auto shadow = DropShadowStyle::Make(8.0f, 8.0f, 5.0f, 5.0f, Color::Black());
    layer->setLayerStyles({noise, shadow});
    back->addChild(layer);
  }

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/WithBlurAndShadow"));
}

TGFX_TEST(LayerFilterTest, ChainedMonoNoise) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto rectWidth = 150.0f;
  auto rectHeight = 100.0f;
  auto padding = 50.0f;
  auto surfaceWidth = static_cast<int>(rectWidth + padding * 2);
  auto surfaceHeight = static_cast<int>(rectHeight + padding * 2);
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);

  auto displayList = std::make_unique<DisplayList>();
  auto layer = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeWH(rectWidth, rectHeight));
  layer->setPath(path);
  layer->setFillStyle(ShapeStyle::Make(Color::White()));
  layer->setMatrix(Matrix::MakeTrans(padding, padding));

  std::vector<std::shared_ptr<LayerFilter>> filters;
  for (int i = 0; i < 10; ++i) {
    float density = 0.1f + static_cast<float>(i) * 0.09f;
    filters.push_back(NoiseFilter::MakeMono(5.0f, density, Color::FromRGBA(128, 64, 32, 100),
                                            static_cast<float>(i * 7), BlendMode::SrcOver));
  }
  layer->setFilters(filters);

  displayList->root()->addChild(layer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/ChainedMonoNoise"));
}

TGFX_TEST(LayerFilterTest, DropShadowBlurDuoNoise) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  constexpr int rectSize = 120;
  constexpr int margin = 50;
  constexpr int canvasWidth = margin * 2 + rectSize;
  constexpr int canvasHeight = margin * 2 + rectSize;

  auto displayList = std::make_unique<DisplayList>();

  auto layer = ShapeLayer::Make();
  Path rect;
  rect.addRect(Rect::MakeWH(static_cast<float>(rectSize), static_cast<float>(rectSize)));
  layer->setPath(rect);
  layer->setMatrix(Matrix::MakeTrans(static_cast<float>(margin), static_cast<float>(margin)));
  layer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(60, 120, 200)));

  auto dropShadow = DropShadowFilter::Make(6.0f, 6.0f, 4.0f, 4.0f, Color::Black());
  auto blur = BlurFilter::Make(3.0f, 3.0f);
  auto duoNoise = NoiseFilter::MakeDuo(8.0f, 1.0f, Color::FromRGBA(255, 0, 0, 128),
                                       Color::FromRGBA(0, 0, 255, 128), 42.0f, BlendMode::SrcOver);
  layer->setFilters({dropShadow, blur, duoNoise});

  displayList->root()->addChild(layer);

  auto surface = Surface::Make(context, canvasWidth, canvasHeight);
  ASSERT_TRUE(surface != nullptr);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/DropShadowBlurDuoNoise"));
}

TGFX_TEST(LayerFilterTest, TextLayerNoiseBoundsDiagnosis) {
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSansSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);

  float fontSize = 40.0f;
  Font font(typeface, fontSize);

  // Build a TextBlob for "Ag" to test bounds
  const char* text = "Ag";
  auto blob = TextBlob::MakeFrom(text, font);
  ASSERT_TRUE(blob != nullptr);

  auto bounds = blob->getBounds();
  auto tightBounds = blob->getTightBounds();

  LOGI("=== TextBlob 'Ag' (fontSize=%.1f) ===", fontSize);
  LOGI("  getBounds():      left=%.2f top=%.2f right=%.2f bottom=%.2f  [w=%.2f h=%.2f]",
       bounds.left, bounds.top, bounds.right, bounds.bottom, bounds.width(), bounds.height());
  LOGI("  getTightBounds(): left=%.2f top=%.2f right=%.2f bottom=%.2f  [w=%.2f h=%.2f]",
       tightBounds.left, tightBounds.top, tightBounds.right, tightBounds.bottom,
       tightBounds.width(), tightBounds.height());
  LOGI("  getBounds center:      (%.2f, %.2f)", bounds.centerX(), bounds.centerY());
  LOGI("  getTightBounds center: (%.2f, %.2f)", tightBounds.centerX(), tightBounds.centerY());

  // Simulate what Layer::computeBounds() does for TextContent
  // TextContent::onGetBounds() returns textBlob->getBounds().makeOffset(offset)
  // For a TextLayer with no offset, this is just textBlob->getBounds()
  auto contentBounds = bounds;  // This is what Layer passes to NoiseFilter
  LOGI("  contentBounds (passed to NoiseFilter): left=%.2f top=%.2f right=%.2f bottom=%.2f",
       contentBounds.left, contentBounds.top, contentBounds.right, contentBounds.bottom);
  LOGI("  NoiseFilter shift (contentBounds.center): (%.2f, %.2f)", contentBounds.centerX(),
       contentBounds.centerY());
  LOGI("  Ideal shift (tightBounds.center):         (%.2f, %.2f)", tightBounds.centerX(),
       tightBounds.centerY());
  LOGI("  Shift error: dx=%.2f dy=%.2f", contentBounds.centerX() - tightBounds.centerX(),
       contentBounds.centerY() - tightBounds.centerY());

  // Also test with a Chinese character
  blob = TextBlob::MakeFrom("你", font);
  ASSERT_TRUE(blob != nullptr);
  bounds = blob->getBounds();
  tightBounds = blob->getTightBounds();

  LOGI("=== TextBlob '你' (fontSize=%.1f) ===", fontSize);
  LOGI("  getBounds():      left=%.2f top=%.2f right=%.2f bottom=%.2f  [w=%.2f h=%.2f]",
       bounds.left, bounds.top, bounds.right, bounds.bottom, bounds.width(), bounds.height());
  LOGI("  getTightBounds(): left=%.2f top=%.2f right=%.2f bottom=%.2f  [w=%.2f h=%.2f]",
       tightBounds.left, tightBounds.top, tightBounds.right, tightBounds.bottom,
       tightBounds.width(), tightBounds.height());
  LOGI("  getBounds center:      (%.2f, %.2f)", bounds.centerX(), bounds.centerY());
  LOGI("  getTightBounds center: (%.2f, %.2f)", tightBounds.centerX(), tightBounds.centerY());
  LOGI("  Shift error: dx=%.2f dy=%.2f", bounds.centerX() - tightBounds.centerX(),
       bounds.centerY() - tightBounds.centerY());

  // Now render a TextLayer with NoiseFilter for visual comparison
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto textLayer = TextLayer::Make();
  textLayer->setText("Ag");
  textLayer->setFont(font);
  textLayer->setTextColor(Color::FromRGBA(60, 120, 200));

  auto noise =
      NoiseFilter::MakeMono(8.0f, 1.0f, Color::FromRGBA(0, 0, 0, 128), 42.0f, BlendMode::SrcOver);
  textLayer->setFilters({noise});

  // Compute actual layer bounds for surface sizing
  auto displayList = std::make_unique<DisplayList>();
  displayList->root()->addChild(textLayer);

  constexpr int margin = 50;
  auto layerBounds = textLayer->getBounds();
  int canvasWidth = static_cast<int>(ceilf(layerBounds.width())) + margin * 2;
  int canvasHeight = static_cast<int>(ceilf(layerBounds.height())) + margin * 2;
  textLayer->setMatrix(Matrix::MakeTrans(static_cast<float>(margin) - layerBounds.left,
                                         static_cast<float>(margin) - layerBounds.top));

  LOGI("=== TextLayer with NoiseFilter ===");
  LOGI("  layerBounds: left=%.2f top=%.2f right=%.2f bottom=%.2f  [w=%.2f h=%.2f]",
       layerBounds.left, layerBounds.top, layerBounds.right, layerBounds.bottom,
       layerBounds.width(), layerBounds.height());

  auto surface = Surface::Make(context, canvasWidth, canvasHeight);
  ASSERT_TRUE(surface != nullptr);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerFilterTest/TextLayerNoiseBoundsDiagnosis"));
}

}  // namespace tgfx
