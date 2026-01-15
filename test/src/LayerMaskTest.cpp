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

#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/filters/ColorMatrixFilter.h"
#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"
#include "utils/TestUtils.h"
#include "utils/common.h"

namespace tgfx {

TGFX_TEST(LayerMaskTest, imageMask) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/rotation.jpg");
  auto surface = Surface::Make(context, image->width(), static_cast<int>(image->height() * 1.5));
  auto displayList = std::make_unique<DisplayList>();
  auto layer = Layer::Make();
  displayList->root()->addChild(layer);

  auto maskImage = MakeImage("resources/apitest/test_timestretch.png");

  // Original image
  auto originalLayer = Layer::Make();
  layer->addChild(originalLayer);
  auto imageLayer0 = ImageLayer::Make();
  originalLayer->addChild(imageLayer0);
  imageLayer0->setImage(image);
  imageLayer0->setMatrix(Matrix::MakeScale(0.5f));

  auto scrollRect = Rect::MakeXYWH(200, 200, 2600, 3600);
  imageLayer0->setScrollRect(scrollRect);

  auto imageLayer = ImageLayer::Make();
  originalLayer->addChild(imageLayer);
  imageLayer->setImage(maskImage);
  imageLayer->setAlpha(1.0f);
  Matrix maskMatrix = Matrix::MakeAll(1.2f, 0, 0, 0, 1.2f, 500);
  imageLayer->setMatrix(maskMatrix);

  auto originalLayerBounds = originalLayer->getBounds();
  EXPECT_TRUE(originalLayerBounds == Rect::MakeXYWH(0, 0, 1536, 1800));

  // Alpha mask effect
  auto alphaLayer = Layer::Make();
  layer->addChild(alphaLayer);
  auto imageLayer1 = ImageLayer::Make();
  alphaLayer->addChild(imageLayer1);
  imageLayer1->setImage(image);
  auto image1Matrix =
      Matrix::MakeAll(0.5f, 0, static_cast<float>(image->width()) * 0.5f, 0, 0.5f, 0);
  imageLayer1->setMatrix(image1Matrix);
  imageLayer1->setAlpha(1.0f);
  imageLayer1->setScrollRect(scrollRect);

  auto alphaMaskImageLayer = ImageLayer::Make();
  alphaLayer->addChild(alphaMaskImageLayer);
  alphaMaskImageLayer->setImage(maskImage);
  imageLayer1->setAlpha(0.5f);
  Matrix alphaMaskMatrix =
      Matrix::MakeAll(1.2f, 0, static_cast<float>(image->width()) * 0.5f, 0, 1.2f, 500);
  alphaMaskImageLayer->setMatrix(alphaMaskMatrix);
  imageLayer1->setMask(alphaMaskImageLayer);

  auto alphaLayerBounds = alphaLayer->getBounds();
  EXPECT_TRUE(alphaLayerBounds == Rect::MakeXYWH(1512, 500, 1300, 864));

  // Vector mask effect
  auto imageLayer2 = ImageLayer::Make();
  layer->addChild(imageLayer2);
  imageLayer2->setImage(image);
  auto image2Matrix =
      Matrix::MakeAll(0.5f, 0, 0, 0, 0.5f, static_cast<float>(image->height()) * 0.5f);
  imageLayer2->setMatrix(image2Matrix);
  imageLayer2->setAlpha(1.0f);
  imageLayer2->setScrollRect(scrollRect);

  auto vectorMaskImageLayer = ImageLayer::Make();
  layer->addChild(vectorMaskImageLayer);
  vectorMaskImageLayer->setImage(maskImage);
  imageLayer2->setMask(vectorMaskImageLayer);
  imageLayer2->setMaskType(LayerMaskType::Contour);
  imageLayer2->setAlpha(0.5f);
  Matrix vectorMaskMatrix =
      Matrix::MakeAll(1.2f, 0, 0, 0, 1.2f, 500 + static_cast<float>(image->height()) * 0.5f);
  vectorMaskImageLayer->setMatrix(vectorMaskMatrix);

  // Luma mask Effect
  auto imageLayer3 = ImageLayer::Make();
  layer->addChild(imageLayer3);
  imageLayer3->setImage(image);
  auto image3Matrix = Matrix::MakeAll(0.5f, 0, static_cast<float>(image->width()) * 0.5f, 0, 0.5f,
                                      static_cast<float>(image->height()) * 0.5f);
  imageLayer3->setMatrix(image3Matrix);
  imageLayer3->setAlpha(1.0f);
  imageLayer3->setScrollRect(scrollRect);

  auto lumaMaskImageLayer = ImageLayer::Make();
  layer->addChild(lumaMaskImageLayer);
  lumaMaskImageLayer->setImage(maskImage);
  imageLayer3->setMask(lumaMaskImageLayer);
  imageLayer3->setMaskType(LayerMaskType::Luminance);
  imageLayer3->setAlpha(0.5f);
  Matrix lumaMaskMatrix = Matrix::MakeAll(1.2f, 0, static_cast<float>(image->width()) * 0.5f, 0,
                                          1.2f, 500 + static_cast<float>(image->height()) * 0.5f);
  lumaMaskImageLayer->setMatrix(lumaMaskMatrix);

  // The layer and its mask have no intersection.
  auto imageLayer4 = ImageLayer::Make();
  layer->addChild(imageLayer4);
  imageLayer4->setImage(image);
  auto image4Matrix = Matrix::MakeAll(0.5f, 0, 0, 0, 0.5f, static_cast<float>(image->height()));
  imageLayer4->setMatrix(image4Matrix);

  auto maskImageLayer4 = ImageLayer::Make();
  layer->addChild(maskImageLayer4);
  maskImageLayer4->setImage(maskImage);
  maskImageLayer4->setMatrix(Matrix::MakeAll(1.2f, 0, static_cast<float>(image->width()) * 0.5f, 0,
                                             1.2f, 500 + static_cast<float>(image->height())));
  imageLayer4->setMask(maskImageLayer4);

  // The Layer's scroll rect and its mask have no intersection
  auto imageLayer5 = ImageLayer::Make();
  layer->addChild(imageLayer5);
  imageLayer5->setImage(image);
  auto image5Matrix = Matrix::MakeAll(0.5f, 0, static_cast<float>(image->width()) * 0.5f, 0, 0.5f,
                                      static_cast<float>(image->height()));
  imageLayer5->setMatrix(image5Matrix);
  auto imageLayer5ScrollRect = Rect::MakeXYWH(100, 100, 1200, 1000);
  imageLayer5->setScrollRect(imageLayer5ScrollRect);

  auto maskImageLayer5 = ImageLayer::Make();
  layer->addChild(maskImageLayer5);
  maskImageLayer5->setImage(maskImage);
  maskImageLayer5->setMatrix(Matrix::MakeAll(1.2f, 0, static_cast<float>(image->width()) * 0.5f, 0,
                                             1.2f, 500 + static_cast<float>(image->height())));
  imageLayer5->setMask(maskImageLayer5);

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerMaskTest/imageMask"));
}

TGFX_TEST(LayerMaskTest, shapeMask) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/rotation.jpg");
  auto surface = Surface::Make(context, image->width(), image->height());
  auto displayList = std::make_unique<DisplayList>();
  auto layer = Layer::Make();
  displayList->root()->addChild(layer);

  auto rect = Rect::MakeXYWH(0, 0, 1000, 1000);
  Path path = {};
  path.addRoundRect(rect, 200, 200);

  // Original image
  auto imageLayer0 = ImageLayer::Make();
  layer->addChild(imageLayer0);
  imageLayer0->setImage(image);
  imageLayer0->setMatrix(Matrix::MakeScale(0.5f));

  auto shaperLayer = ShapeLayer::Make();
  shaperLayer->setPath(path);
  auto radialFilleStyle = ShapeStyle::Make(
      Shader::MakeRadialGradient({500, 500}, 500, {{1.f, 0.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f}}));
  shaperLayer->setFillStyle(radialFilleStyle);
  shaperLayer->setAlpha(0.5f);
  layer->addChild(shaperLayer);
  Matrix maskMatrix = Matrix::MakeAll(1.0f, 0, 300, 0, 1.0f, 300);
  shaperLayer->setMatrix(maskMatrix);

  // Alpha mask effect
  auto imageLayer1 = ImageLayer::Make();
  layer->addChild(imageLayer1);
  imageLayer1->setImage(image);
  auto image1Matrix =
      Matrix::MakeAll(0.5f, 0, static_cast<float>(image->width()) * 0.5f, 0, 0.5f, 0);
  imageLayer1->setMatrix(image1Matrix);
  imageLayer1->setAlpha(1.0f);

  auto alphaShaperLayer = ShapeLayer::Make();
  alphaShaperLayer->setPath(path);
  auto filleStyle = ShapeStyle::Make(Color::Red());
  alphaShaperLayer->setFillStyle(filleStyle);
  alphaShaperLayer->setAlpha(0.5f);
  layer->addChild(alphaShaperLayer);
  Matrix alphaMaskMatrix =
      Matrix::MakeAll(1.0f, 0, 300 + static_cast<float>(image->width()) * 0.5f, 0, 1.0f, 300);
  alphaShaperLayer->setMatrix(alphaMaskMatrix);
  imageLayer1->setMask(alphaShaperLayer);
  imageLayer1->setMaskType(LayerMaskType::Alpha);

  // Vector mask effect
  auto imageLayer2 = ImageLayer::Make();
  layer->addChild(imageLayer2);
  imageLayer2->setImage(image);
  auto image2Matrix =
      Matrix::MakeAll(0.5f, 0, 0, 0, 0.5f, static_cast<float>(image->height()) * 0.5f);
  imageLayer2->setMatrix(image2Matrix);
  imageLayer2->setAlpha(1.0f);
  imageLayer2->setMaskType(LayerMaskType::Contour);

  auto vectorShaperLayer = ShapeLayer::Make();
  vectorShaperLayer->setPath(path);
  // make a fill style with alpha
  auto vectorFillStyle = ShapeStyle::Make(Color::FromRGBA(0, 0, 255, 128));
  vectorShaperLayer->setFillStyle(vectorFillStyle);
  layer->addChild(vectorShaperLayer);
  Matrix vectorMaskMatrix =
      Matrix::MakeAll(1.0f, 0, 300, 0, 1.0f, 300 + static_cast<float>(image->height()) * 0.5f);
  vectorShaperLayer->setMatrix(vectorMaskMatrix);
  imageLayer2->setMask(vectorShaperLayer);

  // Luma mask Effect
  auto imageLayer3 = ImageLayer::Make();
  layer->addChild(imageLayer3);
  imageLayer3->setImage(image);
  auto image3Matrix = Matrix::MakeAll(0.5f, 0, static_cast<float>(image->width()) * 0.5f, 0, 0.5f,
                                      static_cast<float>(image->height()) * 0.5f);
  imageLayer3->setMatrix(image3Matrix);
  imageLayer3->setAlpha(1.0f);
  imageLayer3->setMaskType(LayerMaskType::Luminance);

  auto lumaShaperLayer = ShapeLayer::Make();
  lumaShaperLayer->setPath(path);
  lumaShaperLayer->setFillStyle(filleStyle);
  lumaShaperLayer->setAlpha(0.5f);
  layer->addChild(lumaShaperLayer);
  Matrix lumaMaskMatrix =
      Matrix::MakeAll(1.0f, 0, 300 + static_cast<float>(image->width()) * 0.5f, 0, 1.0f,
                      300 + static_cast<float>(image->height()) * 0.5f);
  lumaShaperLayer->setMatrix(lumaMaskMatrix);
  imageLayer3->setMask(lumaShaperLayer);

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerMaskTest/shapeMask"));
}

TGFX_TEST(LayerMaskTest, textMask) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/rotation.jpg");
  auto surface = Surface::Make(context, image->width(), image->height());
  auto displayList = std::make_unique<DisplayList>();
  auto layer = Layer::Make();
  displayList->root()->addChild(layer);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  tgfx::Font font(typeface, 100);
  auto textContent = "Hello, TGFX! \n Mask Test!";
  auto color = Color::Red();

  // Original image
  auto originalLayer = Layer::Make();
  layer->addChild(originalLayer);
  auto imageLayer0 = ImageLayer::Make();
  originalLayer->addChild(imageLayer0);
  imageLayer0->setImage(image);
  imageLayer0->setMatrix(Matrix::MakeScale(0.5f));

  auto textLayer = TextLayer::Make();
  originalLayer->addChild(textLayer);
  textLayer->setText(textContent);
  textLayer->setTextColor(color);
  textLayer->setFont(font);
  textLayer->setAlpha(1.0f);
  auto textLayerMatrix = Matrix::MakeAll(1.5f, 0, 400, 0, 1.5f, 800);
  textLayer->setMatrix(textLayerMatrix);

  auto originalLayerBounds = originalLayer->getBounds();
  EXPECT_TRUE(originalLayerBounds == Rect::MakeXYWH(0.f, 0.f, 1694.2f, 2016.f));

  // Alpha mask effect
  auto alphaLayer = Layer::Make();
  layer->addChild(alphaLayer);
  auto imageLayer1 = ImageLayer::Make();
  alphaLayer->addChild(imageLayer1);
  imageLayer1->setImage(image);
  auto image1Matrix =
      Matrix::MakeAll(0.5f, 0, static_cast<float>(image->width()) * 0.5f, 0, 0.5f, 0);
  imageLayer1->setMatrix(image1Matrix);
  imageLayer1->setAlpha(1.0f);

  auto alphaTextLayer = TextLayer::Make();
  alphaLayer->addChild(alphaTextLayer);
  alphaTextLayer->setText(textContent);
  alphaTextLayer->setTextColor(color);
  auto alphaFilter = ColorMatrixFilter::Make(alphaColorMatrix);
  alphaTextLayer->setFilters({alphaFilter});
  alphaTextLayer->setFont(font);
  alphaTextLayer->setAlpha(1.0f);
  auto alphaTextLayerMatrix =
      Matrix::MakeAll(1.5f, 0, 400 + static_cast<float>(image->width()) * 0.5f, 0, 1.5f, 800);
  alphaTextLayer->setMatrix(alphaTextLayerMatrix);
  imageLayer1->setMask(alphaTextLayer);

  auto alphaLayerBounds = alphaLayer->getBounds();
  EXPECT_EQ(alphaLayerBounds, Rect::MakeLTRB(1760.5f, 746, 3024, 1392.5f));

  // Vector mask effect
  auto imageLayer2 = ImageLayer::Make();
  layer->addChild(imageLayer2);
  imageLayer2->setImage(image);
  auto image2Matrix =
      Matrix::MakeAll(0.5f, 0, 0, 0, 0.5f, static_cast<float>(image->height()) * 0.5f);
  imageLayer2->setMatrix(image2Matrix);
  imageLayer2->setAlpha(1.0f);

  auto vectorTextLayer = TextLayer::Make();
  layer->addChild(vectorTextLayer);
  vectorTextLayer->setText(textContent);
  vectorTextLayer->setTextColor(color);
  vectorTextLayer->setFont(font);
  vectorTextLayer->setAlpha(1.0f);
  auto vectorTextLayerMatrix =
      Matrix::MakeAll(1.5f, 0, 400, 0, 1.5f, 800 + static_cast<float>(image->height()) * 0.5f);
  vectorTextLayer->setMatrix(vectorTextLayerMatrix);
  imageLayer2->setMask(vectorTextLayer);

  // Luma mask Effect
  auto imageLayer3 = ImageLayer::Make();
  layer->addChild(imageLayer3);
  imageLayer3->setImage(image);
  auto image3Matrix = Matrix::MakeAll(0.5f, 0, static_cast<float>(image->width()) * 0.5f, 0, 0.5f,
                                      static_cast<float>(image->height()) * 0.5f);
  imageLayer3->setMatrix(image3Matrix);
  imageLayer3->setAlpha(1.0f);

  auto lumaTextLayer = TextLayer::Make();
  layer->addChild(lumaTextLayer);
  lumaTextLayer->setText(textContent);
  lumaTextLayer->setTextColor(color);
  auto lumaFilter = ColorMatrixFilter::Make(lumaColorMatrix);
  lumaTextLayer->setFilters({lumaFilter});
  lumaTextLayer->setFont(font);
  lumaTextLayer->setAlpha(1.0f);
  auto lumaTextLayerMatrix =
      Matrix::MakeAll(1.5f, 0, 400 + static_cast<float>(image->width()) * 0.5f, 0, 1.5f,
                      800 + static_cast<float>(image->height()) * 0.5f);
  lumaTextLayer->setMatrix(lumaTextLayerMatrix);
  imageLayer3->setMask(lumaTextLayer);

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerMaskTest/textMask"));
}

TGFX_TEST(LayerMaskTest, MaskOnwer) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1, 1);
  auto displayList = std::make_unique<DisplayList>();
  auto layer = SolidLayer::Make();
  layer->setWidth(1);
  layer->setHeight(1);
  auto layer2 = SolidLayer::Make();
  layer2->setWidth(1);
  layer2->setHeight(1);
  auto mask = ShapeLayer::Make();
  Path path = {};
  path.addRect(Rect::MakeWH(1, 1));
  mask->setPath(path);
  mask->setFillStyle(ShapeStyle::Make(Color::White()));

  displayList->root()->addChild(layer);
  layer->addChild(layer2);
  displayList->root()->addChild(mask);

  layer->setMask(mask);
  EXPECT_EQ(layer->mask(), mask);
  EXPECT_EQ(mask->maskOwner, layer.get());

  layer2->setMask(mask);
  EXPECT_EQ(layer->mask(), nullptr);
  EXPECT_EQ(mask->maskOwner, layer2.get());

  EXPECT_TRUE(layer2->bitFields.dirtyContent);
  displayList->render(surface.get());
  EXPECT_FALSE(layer->bitFields.dirtyDescendents);
  mask->setAlpha(0.5f);
  EXPECT_TRUE(layer->bitFields.dirtyDescendents);

  layer2->setMask(nullptr);
  EXPECT_EQ(layer->mask(), nullptr);
  EXPECT_EQ(mask->maskOwner, nullptr);
}

TGFX_TEST(LayerMaskTest, MaskAlpha) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  Path path;
  path.addRect(Rect::MakeWH(100, 100));

  auto layer = ShapeLayer::Make();
  layer->setPath(path);
  auto layer_style = tgfx::ShapeStyle::Make({0.0f, 1.0f, 0.0f, 1.0f});
  layer->setFillStyle(layer_style);

  auto mask = ShapeLayer::Make();
  mask->setPath(path);
  mask->setMatrix(Matrix::MakeTrans(50, 50));
  auto mask_style = tgfx::ShapeStyle::Make({1.0f, 0.0f, 0.0f, 0.0f});
  mask->setFillStyle(mask_style);

  layer->setMask(mask);

  list.root()->addChild(layer);
  list.root()->addChild(mask);
  auto surface = Surface::Make(context, 150, 150);
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerMaskTest/MaskAlpha"));
}

TGFX_TEST(LayerMaskTest, ChildMask) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  Path path;
  path.addRect(Rect::MakeWH(100, 100));

  const auto init_trans = Matrix::MakeTrans(150, 50);

  auto group = ShapeLayer::Make();

  auto layer = ShapeLayer::Make();
  layer->setPath(path);
  auto layer_matrix = Matrix::MakeRotate(45);
  layer_matrix.postConcat(init_trans);
  layer->setMatrix(layer_matrix);
  auto layer_style = ShapeStyle::Make(Color::Red());
  layer->setFillStyle(layer_style);

  auto layer2 = ShapeLayer::Make();
  layer2->setPath(path);
  auto layer2_matrix = Matrix::MakeTrans(100, 0);
  layer2_matrix.postConcat(init_trans);
  layer2->setMatrix(layer2_matrix);
  auto layer2_style = ShapeStyle::Make(Color::Green());
  layer2->setFillStyle(layer2_style);

  auto mask = ShapeLayer::Make();
  mask->setPath(path);
  auto mask_matrix = Matrix::MakeTrans(50, 50);
  mask_matrix.postConcat(init_trans);
  mask->setMatrix(mask_matrix);
  auto mask_style = ShapeStyle::Make(Color::Blue());
  mask->setFillStyle(mask_style);

  group->addChild(layer);
  group->addChild(layer2);
  group->addChild(mask);

  group->setMask(mask);

  auto groupMatrix = Matrix::MakeScale(0.5f);
  groupMatrix.postRotate(30);
  group->setMatrix(groupMatrix);

  group->setFilters({BlurFilter::Make(10, 10)});

  list.root()->addChild(group);
  auto surface = Surface::Make(context, 300, 300);
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerMaskTest/ChildMask"));
}

TGFX_TEST(LayerMaskTest, InvalidMask) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  Path path;
  path.addRect(Rect::MakeWH(10, 10));
  auto shapeLayer = ShapeLayer::Make();
  shapeLayer->setPath(path);
  auto fillStyle = ShapeStyle::Make(Color::Red());
  shapeLayer->setFillStyle(fillStyle);

  auto maskLayer = ShapeLayer::Make();
  maskLayer->setPath(path);
  auto maskFillStyle = ShapeStyle::Make(Color::FromRGBA(0, 0, 0, 128));
  maskLayer->setFillStyle(maskFillStyle);
  maskLayer->setVisible(false);

  shapeLayer->setMask(maskLayer);

  list.root()->addChild(shapeLayer);
  list.root()->addChild(maskLayer);

  auto surface = Surface::Make(context, 10, 10);

  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerMaskTest/InvalidMask"));
}

TGFX_TEST(LayerMaskTest, HighZoomWithMask) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto proxy =
      RenderTargetProxy::Make(context, 1622, 1436, false, 1, false, ImageOrigin::BottomLeft);
  auto surface = Surface::MakeFrom(std::move(proxy), 0, true);
  auto displayList = std::make_unique<DisplayList>();

  // Root layer with matrix3D transform
  auto root = Layer::Make();
  // Matrix3D: scale(3.0913887, 3.0913887), translate(347.291687, 99.7222595)
  root->setMatrix(Matrix::MakeAll(3.0913887f, 0, 347.291687f, 0, 3.0913887f, 99.7222595f));
  displayList->root()->addChild(root);

  // Layer 1: Blue rectangle with mask
  auto rectLayer = ShapeLayer::Make();
  Path rectPath;
  rectPath.addRect(Rect::MakeXYWH(50, 50, 300, 400));
  rectLayer->setPath(rectPath);
  rectLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(72, 154, 209)));

  auto rectMask = ShapeLayer::Make();
  rectMask->setPath(rectPath);
  rectMask->setFillStyle(ShapeStyle::Make(Color::White()));
  rectLayer->setMask(rectMask);

  root->addChild(rectLayer);
  root->addChild(rectMask);

  // Layer 2: Red rounded rectangle with mask, child of rectLayer
  auto roundRectLayer = ShapeLayer::Make();
  Path roundRectPath;
  roundRectPath.addRoundRect(Rect::MakeXYWH(80, 100, 200, 250), 30, 30);
  roundRectLayer->setPath(roundRectPath);
  roundRectLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(233, 100, 100)));

  auto roundRectMask = ShapeLayer::Make();
  roundRectMask->setPath(roundRectPath);
  roundRectMask->setFillStyle(ShapeStyle::Make(Color::White()));
  roundRectLayer->setMask(roundRectMask);

  rectLayer->addChild(roundRectLayer);
  rectLayer->addChild(roundRectMask);

  // Layer 3: Inner rect with mask, child of roundRectLayer
  auto innerRectLayer = ShapeLayer::Make();
  Path innerRectPath;
  innerRectPath.addRect(Rect::MakeXYWH(100, 130, 150, 180));
  innerRectLayer->setPath(innerRectPath);
  innerRectLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(200, 200, 100)));

  auto innerRectMask = ShapeLayer::Make();
  innerRectMask->setPath(innerRectPath);
  innerRectMask->setFillStyle(ShapeStyle::Make(Color::White()));
  innerRectLayer->setMask(innerRectMask);

  roundRectLayer->addChild(innerRectLayer);
  roundRectLayer->addChild(innerRectMask);

  // Layer 4: Green rectangle, child of innerRectLayer
  auto greenRectLayer = ShapeLayer::Make();
  Path greenRectPath;
  greenRectPath.addRect(Rect::MakeXYWH(120, 150, 100, 120));
  greenRectLayer->setPath(greenRectPath);
  greenRectLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(100, 200, 100)));

  innerRectLayer->addChild(greenRectLayer);

  // Layer 5: Background blur layer, child of rectLayer (on top)
  auto backgroundBlurLayer = SolidLayer::Make();
  backgroundBlurLayer->setColor(Color::FromRGBA(255, 255, 255, 50));
  backgroundBlurLayer->setWidth(150);
  backgroundBlurLayer->setHeight(150);
  backgroundBlurLayer->setMatrix(Matrix::MakeTrans(600, 600));
  backgroundBlurLayer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  rectLayer->addChild(backgroundBlurLayer);
  // Test Tiled mode
  auto proxy2 =
      RenderTargetProxy::Make(context, 1622, 1436, false, 1, false, ImageOrigin::BottomLeft);
  auto surface2 = Surface::MakeFrom(std::move(proxy2), 0, true);
  auto displayList2 = std::make_unique<DisplayList>();
  displayList2->root()->addChild(root);
  displayList2->setRenderMode(RenderMode::Tiled);
  displayList2->render(surface2.get());
  displayList2->setZoomScale(15.381f);
  displayList2->setContentOffset(-9853.69f, -7356.61f);
  displayList2->render(surface2.get());
  EXPECT_TRUE(Baseline::Compare(surface2, "LayerMaskTest/HighZoomWithMask_Tiled"));
}

TGFX_TEST(LayerMaskTest, MaskPathOptimization) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto displayList = std::make_unique<DisplayList>();
  auto rootLayer = displayList->root();

  // Image is 110x110, after 0.5 scale it's 55x55
  // We position each image and its mask to create a 2x2 grid

  // Test 1: SolidLayer (Rect) as mask with Alpha type
  // Image at (0,0)-(55,55), mask at (10,10)-(50,40)
  auto layer1 = ImageLayer::Make();
  layer1->setImage(MakeImage("resources/apitest/imageReplacement.png"));
  layer1->setMatrix(Matrix::MakeScale(0.5f));
  rootLayer->addChild(layer1);

  auto rectMask = SolidLayer::Make();
  rectMask->setWidth(40.f);
  rectMask->setHeight(30.f);
  rectMask->setMatrix(Matrix::MakeTrans(10.f, 10.f));
  rectMask->setColor(Color::White());
  rootLayer->addChild(rectMask);
  layer1->setMask(rectMask);
  layer1->setMaskType(LayerMaskType::Alpha);

  // Test 2: SolidLayer (RRect) as mask with Contour type
  // Image at (100,0)-(155,55), mask at (110,10)-(150,40)
  auto layer2 = ImageLayer::Make();
  layer2->setImage(MakeImage("resources/apitest/imageReplacement.png"));
  layer2->setMatrix(Matrix::MakeTrans(100.f, 0.f) * Matrix::MakeScale(0.5f));
  rootLayer->addChild(layer2);

  auto rrectMask = SolidLayer::Make();
  rrectMask->setWidth(40.f);
  rrectMask->setHeight(30.f);
  rrectMask->setRadiusX(8.f);
  rrectMask->setRadiusY(8.f);
  rrectMask->setMatrix(Matrix::MakeTrans(110.f, 10.f));
  rrectMask->setColor(Color::White());
  rootLayer->addChild(rrectMask);
  layer2->setMask(rrectMask);
  layer2->setMaskType(LayerMaskType::Contour);

  // Test 3: ShapeLayer (Path) as mask with Alpha type
  // Image at (0,100)-(55,155), mask is oval at (10,110)-(50,150)
  auto layer3 = ImageLayer::Make();
  layer3->setImage(MakeImage("resources/apitest/imageReplacement.png"));
  layer3->setMatrix(Matrix::MakeTrans(0.f, 100.f) * Matrix::MakeScale(0.5f));
  rootLayer->addChild(layer3);

  Path ovalPath = {};
  ovalPath.addOval(Rect::MakeXYWH(10.f, 110.f, 40.f, 40.f));
  auto pathMask = ShapeLayer::Make();
  pathMask->setPath(ovalPath);
  pathMask->setFillStyle(ShapeStyle::Make(Color::White()));
  rootLayer->addChild(pathMask);
  layer3->setMask(pathMask);
  layer3->setMaskType(LayerMaskType::Alpha);

  // Test 4: Luminance mask - should NOT use path optimization
  // Image at (100,100)-(155,155), mask at (110,110)-(150,140)
  auto layer4 = ImageLayer::Make();
  layer4->setImage(MakeImage("resources/apitest/imageReplacement.png"));
  layer4->setMatrix(Matrix::MakeTrans(100.f, 100.f) * Matrix::MakeScale(0.5f));
  rootLayer->addChild(layer4);

  auto lumaMask = SolidLayer::Make();
  lumaMask->setWidth(40.f);
  lumaMask->setHeight(30.f);
  lumaMask->setMatrix(Matrix::MakeTrans(110.f, 110.f));
  lumaMask->setColor(Color::White());
  rootLayer->addChild(lumaMask);
  layer4->setMask(lumaMask);
  layer4->setMaskType(LayerMaskType::Luminance);

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerMaskTest/MaskPathOptimization"));
}

/**
 * Test RoundRect mask layer with tiled rendering mode.
 * This verifies that clipRect coordinate space is correct in offscreen rendering.
 */
TGFX_TEST(LayerMaskTest, RoundRectMaskWithTiledRender) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 600, 600);
  DisplayList displayList;

  // Create a white background layer
  auto backgroundLayer = ShapeLayer::Make();
  Path backgroundPath;
  backgroundPath.addRect(Rect::MakeXYWH(0, 0, 300, 300));
  backgroundLayer->setPath(backgroundPath);
  backgroundLayer->setFillStyle(ShapeStyle::Make(Color::White()));

  // Create a rect shape layer as the content to be masked
  auto contentLayer = ShapeLayer::Make();
  Path contentPath;
  contentPath.addRect(Rect::MakeXYWH(0, 0, 250, 250));
  contentLayer->setPath(contentPath);
  contentLayer->setFillStyle(ShapeStyle::Make(Color::Blue()));
  contentLayer->setMatrix(Matrix::MakeTrans(10, 10));

  // Create a round rect mask layer
  auto maskLayer = ShapeLayer::Make();
  Path maskPath;
  maskPath.addRoundRect(Rect::MakeXYWH(20, 20, 200, 200), 30, 30);
  maskLayer->setPath(maskPath);
  maskLayer->setFillStyle(ShapeStyle::Make(Color::White()));

  // Apply mask to content layer
  contentLayer->setMask(maskLayer);

  // Create a container layer with 3D matrix
  auto rootLayer = Layer::Make();
  rootLayer->addChild(backgroundLayer);
  rootLayer->addChild(contentLayer);
  rootLayer->addChild(maskLayer);

  // Apply 3D matrix to container layer
  Matrix3D matrix3D = Matrix3D::MakeScale(3.39277792f, 3.39277792f, 1.0f);
  matrix3D.postTranslate(187.083313f, 82.083313f, 0.0f);
  rootLayer->setMatrix3D(matrix3D);

  displayList.root()->addChild(rootLayer);

  // Render with tiled mode
  displayList.setRenderMode(RenderMode::Tiled);
  displayList.render(surface.get());
  displayList.setZoomScale(1.603f);
  displayList.setAllowZoomBlur(false);
  displayList.setContentOffset(-200.179016f, -221.704529f);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerMaskTest/RoundRectMaskWithTiledRender"));
}

}  // namespace tgfx
