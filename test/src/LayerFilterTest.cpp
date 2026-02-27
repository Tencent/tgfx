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
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/filters/BlendFilter.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/filters/ColorMatrixFilter.h"
#include "tgfx/layers/filters/DropShadowFilter.h"
#include "tgfx/layers/filters/InnerShadowFilter.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "tgfx/layers/layerstyles/InnerShadowStyle.h"
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
  auto bounds = filter4->getImageFilter(1.0f)->filterBounds(src);
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
  EXPECT_EQ(imageFilter->blurrinessX, imageFilter2->blurrinessX);
  EXPECT_EQ(imageFilter->blurrinessY, imageFilter2->blurrinessY);
  EXPECT_EQ(imageFilter->tileMode, imageFilter2->tileMode);

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

}  // namespace tgfx
