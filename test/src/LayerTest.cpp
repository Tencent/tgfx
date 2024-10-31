/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <math.h>
#include <vector>
#include "core/filters/BlurImageFilter.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/filters/BlendFilter.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/filters/ColorMatrixFilter.h"
#include "tgfx/layers/filters/DropShadowFilter.h"
#include "utils/TestUtils.h"

namespace tgfx {
TGFX_TEST(LayerTest, LayerTree) {
  auto displayList = std::make_unique<DisplayList>();
  auto parent = Layer::Make();
  auto child1 = Layer::Make();
  auto child2 = Layer::Make();
  auto child3 = Layer::Make();

  // Test adding children
  parent->addChild(child1);
  child1->addChild(child2);
  EXPECT_EQ(child1->parent(), parent);
  EXPECT_EQ(child2->parent(), child1);
  EXPECT_TRUE(parent->children().size() == 1);
  EXPECT_TRUE(parent->contains(child1));
  EXPECT_TRUE(parent->contains(child2));
  EXPECT_EQ(parent->getChildIndex(child1), 0);
  EXPECT_EQ(parent->getChildIndex(child2), -1);
  parent->addChildAt(child3, 0);
  EXPECT_EQ(child3->parent(), parent);
  EXPECT_TRUE(parent->children().size() == 2);
  EXPECT_TRUE(parent->getChildIndex(child3) == 0);
  EXPECT_EQ(parent->getChildIndex(child1), 1);

  // Tests for setting the display list owner.
  EXPECT_EQ(parent->root(), nullptr);
  EXPECT_EQ(child1->root(), nullptr);
  EXPECT_EQ(child2->root(), nullptr);
  EXPECT_EQ(child3->root(), nullptr);
  displayList->root()->addChild(parent);
  EXPECT_EQ(parent->root(), displayList->root());
  EXPECT_EQ(child1->root(), displayList->root());
  EXPECT_EQ(child2->root(), displayList->root());
  EXPECT_EQ(child3->root(), displayList->root());

  parent->removeFromParent();
  EXPECT_EQ(parent->root(), nullptr);
  EXPECT_EQ(child1->root(), nullptr);
  EXPECT_EQ(child2->root(), nullptr);
  EXPECT_EQ(child3->root(), nullptr);
  displayList->root()->addChild(parent);

  // Test replacing a child
  auto replacedChild = Layer::Make();
  auto replacedChild2 = Layer::Make();
  parent->replaceChild(replacedChild, replacedChild2);
  EXPECT_EQ(replacedChild2->parent(), nullptr);
  EXPECT_EQ(replacedChild2->root(), nullptr);

  parent->replaceChild(child1, replacedChild);
  EXPECT_EQ(replacedChild->parent(), parent);
  EXPECT_EQ(replacedChild->root(), displayList->root());
  EXPECT_FALSE(parent->contains(child1));
  EXPECT_FALSE(parent->contains(child2));
  EXPECT_TRUE(parent->contains(replacedChild));
  EXPECT_TRUE(parent->children().size() == 2);
  EXPECT_EQ(parent->getChildIndex(replacedChild), 1);
  parent->replaceChild(replacedChild, child2);
  EXPECT_EQ(child2->parent(), parent);
  EXPECT_EQ(child2->root(), displayList->root());
  EXPECT_FALSE(parent->contains(replacedChild));
  EXPECT_TRUE(parent->contains(child2));
  EXPECT_TRUE(child1->children().empty());
  parent->addChildAt(child1, 1);

  // Test removing a child
  auto removedChild = parent->removeChildAt(0);
  EXPECT_EQ(removedChild, child3);
  EXPECT_EQ(child3->parent(), nullptr);
  EXPECT_EQ(child3->root(), nullptr);
  EXPECT_FALSE(parent->contains(child3));
  EXPECT_TRUE(parent->children().size() == 2);
  EXPECT_EQ(parent->getChildIndex(child1), 0);
  EXPECT_EQ(parent->getChildIndex(child2), 1);

  // Test removing all children
  parent->removeChildren();
  EXPECT_EQ(child2->parent(), nullptr);
  EXPECT_EQ(child2->root(), nullptr);
  EXPECT_FALSE(parent->contains(child2));
  EXPECT_EQ(child2->parent(), nullptr);
  EXPECT_EQ(child2->root(), nullptr);
  EXPECT_FALSE(parent->contains(child1));
  EXPECT_EQ(child1->parent(), nullptr);
  EXPECT_EQ(child1->root(), nullptr);
  EXPECT_TRUE(parent->children().empty());
}

TGFX_TEST(LayerTest, LayerName) {
  auto layer = Layer::Make();
  EXPECT_EQ(layer->name(), "");
  layer->setName("test");
  EXPECT_EQ(layer->name(), "test");

  auto child = Layer::Make();
  layer->addChild(child);
  EXPECT_EQ(child->name(), "");
  child->setName("child");
  EXPECT_EQ(child->name(), "child");
  EXPECT_EQ(layer->getChildByName("child"), child);

  auto child2 = Layer::Make();
  layer->addChild(child2);
  child2->setName("child");
  EXPECT_EQ(child2->name(), "child");
  EXPECT_EQ(layer->getChildByName("child"), child);

  layer->setChildIndex(child2, 0);
  EXPECT_EQ(layer->getChildByName("child"), child2);
}

TGFX_TEST(LayerTest, LayerTreeCircle) {
  auto parent = Layer::Make();
  auto child = Layer::Make();
  auto grandChild = Layer::Make();
  auto displayList = std::make_unique<DisplayList>();
  // Add child to parent
  EXPECT_TRUE(parent->addChild(child));

  EXPECT_TRUE(child->addChild(grandChild));

  EXPECT_FALSE(child->addChild(parent));

  EXPECT_FALSE(grandChild->addChild(parent));

  EXPECT_FALSE(parent->addChild(displayList->_root));

  EXPECT_FALSE(parent->contains(displayList->_root));

  EXPECT_FALSE(child->contains(parent));

  EXPECT_EQ(displayList->_root, displayList->_root);

  EXPECT_FALSE(grandChild->contains(parent));

  EXPECT_FALSE(child->replaceChild(grandChild, parent));

  EXPECT_FALSE(child->contains(parent));

  EXPECT_TRUE(child->contains(grandChild));

  EXPECT_TRUE(parent->contains(child));
}

TGFX_TEST(LayerTest, textLayer) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto surface = Surface::Make(context, 200, 100);
  auto displayList = std::make_unique<DisplayList>();
  auto layer = Layer::Make();
  displayList->root()->addChild(layer);
  auto textLayer = TextLayer::Make();
  layer->addChild(textLayer);
  layer->setMatrix(Matrix::MakeTrans(10, 10));
  textLayer->setText("Hello, World!");
  auto color = Color::Red();
  color.alpha = 0.5;
  textLayer->setTextColor(color);
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  tgfx::Font font(typeface, 20);
  textLayer->setFont(font);
  textLayer->setAlpha(0.5f);
  textLayer->setMatrix(Matrix::MakeRotate(30));
  auto textLayer2 = TextLayer::Make();
  layer->addChild(textLayer2);
  textLayer2->setText("Hello, World!");
  color.alpha = 0.5;
  textLayer2->setFont(font);
  textLayer2->setBlendMode(BlendMode::Difference);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/draw_text"));
  device->unlock();
}

TGFX_TEST(LayerTest, imageLayer) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto image = MakeImage("resources/apitest/image_as_mask.png");
  auto surface = Surface::Make(context, image->width() * 5, image->height() * 5);
  auto displayList = std::make_unique<DisplayList>();
  auto layer = Layer::Make();
  displayList->root()->addChild(layer);
  auto imageLayer = ImageLayer::Make();
  layer->addChild(imageLayer);
  imageLayer->setImage(image);
  SamplingOptions options(FilterMode::Nearest, MipmapMode::None);
  imageLayer->setSampling(options);
  imageLayer->setMatrix(Matrix::MakeScale(5.0f));
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/imageLayer"));
  device->unlock();
}

TGFX_TEST(LayerTest, Layer_getTotalMatrix) {
  auto parent = Layer::Make();
  // Should have no effect on the total matrix since it has no parent.
  parent->setMatrix(Matrix::MakeTrans(10, 10));

  auto child = Layer::Make();
  child->setMatrix(Matrix::MakeTrans(10, 10));

  auto grandChild = Layer::Make();
  grandChild->setMatrix(Matrix::MakeTrans(10, 10));

  auto greatGrandson = Layer::Make();
  greatGrandson->setMatrix(Matrix::MakeTrans(10, 10));

  parent->addChild(child);
  child->addChild(grandChild);
  grandChild->addChild(greatGrandson);

  auto greatGrandsonTotalMatrix = greatGrandson->getGlobalMatrix();
  EXPECT_EQ(greatGrandsonTotalMatrix, Matrix::MakeTrans(30, 30));

  EXPECT_EQ(greatGrandson->matrix(), Matrix::MakeTrans(10, 10));
  EXPECT_EQ(grandChild->matrix(), Matrix::MakeTrans(10, 10));
  EXPECT_EQ(child->matrix(), Matrix::MakeTrans(10, 10));
  EXPECT_EQ(parent->matrix(), Matrix::MakeTrans(10, 10));

  auto rotateMat = Matrix::MakeRotate(45);
  greatGrandson->setMatrix(rotateMat * greatGrandson->matrix());

  greatGrandsonTotalMatrix = greatGrandson->getGlobalMatrix();
  auto grandChildTotalMatrix = grandChild->getGlobalMatrix();
  EXPECT_FLOAT_EQ(greatGrandsonTotalMatrix.getTranslateX(), grandChildTotalMatrix.getTranslateX());
  EXPECT_FLOAT_EQ(greatGrandsonTotalMatrix.getTranslateY(),
                  grandChildTotalMatrix.getTranslateX() + 10.0f * std::sqrt(2.0f));
}

/**
 * The derivation process is shown in the following figure:
 * https://www.geogebra.org/graphing/vtcatfdf
 * https://codesign-1252678369.cos.ap-guangzhou.myqcloud.com/%E5%9D%90%E6%A0%87%E8%BD%AC%E6%8D%A2_%E5%85%A8%E5%B1%80%E5%88%B0%E5%B1%80%E9%83%A8.png
 */
TGFX_TEST(LayerTest, Layer_globalToLocal) {
  auto layerA1 = Layer::Make();
  layerA1->setMatrix(Matrix::MakeTrans(10.0f, 10.0f));

  auto layerA2 = Layer::Make();
  layerA2->setMatrix(Matrix::MakeTrans(15.0f, 5.0f) * Matrix::MakeRotate(45.0f));

  auto layerA3 = Layer::Make();
  layerA3->setMatrix(Matrix::MakeTrans(10.0f * std::sqrt(2.0f), 5.0f * std::sqrt(2.0f)) *
                     Matrix::MakeRotate(45.0f));

  layerA1->addChild(layerA2);
  layerA2->addChild(layerA3);

  auto globalPoint = Point::Make(15.0f, 35.0f);
  auto pointInLayer3 = layerA3->globalToLocal(globalPoint);
  auto testPoint = Point::Make(15.0f, 5.0f);
  EXPECT_EQ(pointInLayer3, testPoint);

  auto pointInLayer2 = layerA2->globalToLocal(globalPoint);
  testPoint = Point::Make(15.0f * std::sqrt(2.0f), 15.0f * std::sqrt(2.0f));
  EXPECT_FLOAT_EQ(pointInLayer2.x, testPoint.x);
  EXPECT_FLOAT_EQ(pointInLayer2.y, testPoint.y);

  auto pointInLayer1 = layerA1->globalToLocal(globalPoint);
  testPoint = Point::Make(15.0f, 35.0f);
  EXPECT_EQ(pointInLayer1, testPoint);
}
/**
 * The derivation process is shown in the following figure:
 * https://codesign-1252678369.cos.ap-guangzhou.myqcloud.com/%E5%9D%90%E6%A0%87%E8%BD%AC%E6%8D%A2.png
 * https://www.geogebra.org/graphing/kvrqtdqk
 */
TGFX_TEST(LayerTest, Layer_localToGlobal) {
  auto layerA1 = Layer::Make();
  auto mat1 = Matrix::MakeTrans(10, 10);
  layerA1->setMatrix(mat1);

  auto layerA2 = Layer::Make();
  layerA2->setMatrix(Matrix::MakeTrans(10, 10) * Matrix::MakeRotate(45.0f));
  layerA1->addChild(layerA2);
  auto layer2GlobalMat = layerA2->getGlobalMatrix();

  auto layerA3 = Layer::Make();
  layerA3->setMatrix(Matrix::MakeTrans(10 * std::sqrt(2.0f), 10 * std::sqrt(2.0f)) *
                     Matrix::MakeRotate(45.0f));
  layerA2->addChild(layerA3);

  auto pointDInLayer3 = Point::Make(5, 5);
  auto pointDInGlobal = layerA3->localToGlobal(pointDInLayer3);
  EXPECT_FLOAT_EQ(pointDInGlobal.x, 5.0f);
  EXPECT_FLOAT_EQ(pointDInGlobal.y, 35.0f);

  auto pointEInLayer2 = Point::Make(8, 8);
  auto pointEInGlobal = layerA2->localToGlobal(pointEInLayer2);
  EXPECT_EQ(pointEInGlobal, Point::Make(layer2GlobalMat.getTranslateX(),
                                        layer2GlobalMat.getTranslateY() + 8.0f * std::sqrt(2.0f)));

  auto layer4 = Layer::Make();
  layer4->setMatrix(Matrix::MakeTrans(5, -5) * Matrix::MakeRotate(-60.0f));
  layerA3->addChild(layer4);

  auto pointFInLayer4 = Point::Make(10.0f, 10.0f);
  auto pointFInGlobal = layer4->localToGlobal(pointFInLayer4);
  EXPECT_FLOAT_EQ(pointFInGlobal.x, 18.6602554f);
  EXPECT_FLOAT_EQ(pointFInGlobal.y, 48.6602516f);

  auto layer5 = Layer::Make();
  layer5->setMatrix(Matrix::MakeTrans(10, -15) * Matrix::MakeRotate(-90.0f));
  layerA3->addChild(layer5);

  auto pointGInLayer5 = Point::Make(10.0f, 20.0f);
  auto pointGInGlobal = layer5->localToGlobal(pointGInLayer5);
  EXPECT_EQ(pointGInGlobal, Point::Make(35.0f, 60.0f));
}

TGFX_TEST(LayerTest, getbounds) {
  auto root = Layer::Make();
  root->setMatrix(Matrix::MakeTrans(30, 30));

  auto child = TextLayer::Make();
  child->setMatrix(Matrix::MakeRotate(20));
  child->setText("hello");
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  tgfx::Font font(typeface, 20);
  child->setFont(font);
  auto bounds = child->getBounds();
  EXPECT_FLOAT_EQ(bounds.left, 1);
  EXPECT_FLOAT_EQ(bounds.top, 0.43000031f);
  EXPECT_FLOAT_EQ(bounds.right, 47);
  EXPECT_FLOAT_EQ(bounds.bottom, 17.43f);

  auto grandChild = ImageLayer::Make();
  grandChild->setMatrix(Matrix::MakeRotate(40, 55, 55));
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  grandChild->setImage(image);
  bounds = grandChild->getBounds();
  auto clip = Rect::MakeLTRB(10, 10, 70, 70);
  grandChild->setScrollRect(clip);
  EXPECT_FLOAT_EQ(bounds.left, 0);
  EXPECT_FLOAT_EQ(bounds.top, 0);
  EXPECT_EQ(static_cast<int>(bounds.right), image->width());
  EXPECT_EQ(static_cast<int>(bounds.bottom), image->height());

  auto cousin = Layer::Make();
  cousin->setMatrix(Matrix::MakeTrans(10, 10));

  root->addChild(child);
  child->addChild(grandChild);
  root->addChild(cousin);

  bounds = child->getBounds();
  EXPECT_FLOAT_EQ(bounds.left, 1);
  EXPECT_FLOAT_EQ(bounds.top, -22.485762f);
  EXPECT_FLOAT_EQ(bounds.right, 94.183533f);
  EXPECT_FLOAT_EQ(bounds.bottom, 62.044159f);
  bounds = child->getBounds(root.get());
  EXPECT_FLOAT_EQ(bounds.left, -20.280657f);
  EXPECT_FLOAT_EQ(bounds.top, -20.787683f);
  EXPECT_FLOAT_EQ(bounds.right, 96.194153f);
  EXPECT_FLOAT_EQ(bounds.bottom, 90.515099f);
  bounds = child->getBounds(cousin.get());
  EXPECT_FLOAT_EQ(bounds.left, -30.280657f);
  EXPECT_FLOAT_EQ(bounds.top, -30.787683f);
  EXPECT_FLOAT_EQ(bounds.right, 86.194153f);
  EXPECT_FLOAT_EQ(bounds.bottom, 80.515099f);

  auto displayList = std::make_unique<DisplayList>();
  displayList->root()->addChild(root);
  bounds = child->getBounds();
  EXPECT_FLOAT_EQ(bounds.left, 1);
  EXPECT_FLOAT_EQ(bounds.top, -22.485762f);
  EXPECT_FLOAT_EQ(bounds.right, 94.183533f);
  EXPECT_FLOAT_EQ(bounds.bottom, 62.044159f);
  bounds = child->getBounds(root.get());
  EXPECT_FLOAT_EQ(bounds.left, -20.280657f);
  EXPECT_FLOAT_EQ(bounds.top, -20.787683f);
  EXPECT_FLOAT_EQ(bounds.right, 96.194153f);
  EXPECT_FLOAT_EQ(bounds.bottom, 90.515099f);
  bounds = child->getBounds(cousin.get());
  EXPECT_FLOAT_EQ(bounds.left, -30.280657f);
  EXPECT_FLOAT_EQ(bounds.top, -30.787683f);
  EXPECT_FLOAT_EQ(bounds.right, 86.194153f);
  EXPECT_FLOAT_EQ(bounds.bottom, 80.515099f);

  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto rootBounds = root->getBounds();
  auto width = static_cast<int>(rootBounds.width());
  auto height = static_cast<int>(rootBounds.height());
  auto surface = Surface::Make(context, width, height);
  displayList->render(surface.get());
  context->submit();
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/getBounds"));
  device->unlock();
}

TGFX_TEST(LayerTest, shapeLayer) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto surface = Surface::Make(context, 200, 100);
  auto displayList = std::make_unique<DisplayList>();
  auto layer = Layer::Make();
  displayList->root()->addChild(layer);
  auto shaperLayer = ShapeLayer::Make();
  auto rect = Rect::MakeXYWH(10, 10, 150, 80);
  Path path = {};
  path.addRect(rect);
  shaperLayer->setPath(path);
  auto filleStyle = SolidColor::Make(Color::Blue());
  shaperLayer->setFillStyle(filleStyle);
  // stroke style
  shaperLayer->setLineWidth(10.0f);
  shaperLayer->setLineCap(LineCap::Butt);
  shaperLayer->setLineJoin(LineJoin::Miter);
  shaperLayer->setMiterLimit(2.0f);
  auto strokeStyle = SolidColor::Make(Color::Red());
  shaperLayer->setStrokeStyle(strokeStyle);
  std::vector<float> dashPattern = {10.0f, 10.0f};
  shaperLayer->setLineDashPattern(dashPattern);
  shaperLayer->setLineDashPhase(0.0f);

  layer->addChild(shaperLayer);
  auto shapeLayerRect = shaperLayer->getBounds();
  auto bounds = Rect::MakeXYWH(5, 5, 160, 90);
  ASSERT_TRUE(shapeLayerRect == bounds);

  displayList->render(surface.get());
  context->submit();
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/draw_shape"));
  device->unlock();
}

TGFX_TEST(LayerTest, solidLayer) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto surface = Surface::Make(context, 200, 100);
  auto displayList = std::make_unique<DisplayList>();
  auto layer = Layer::Make();
  displayList->root()->addChild(layer);
  auto solidLayer = SolidLayer::Make();
  solidLayer->setWidth(150);
  solidLayer->setHeight(80);
  solidLayer->setRadiusX(30);
  solidLayer->setRadiusY(40);
  solidLayer->setColor(Color::Blue());
  layer->addChild(solidLayer);
  auto bounds = Rect::MakeXYWH(0, 0, 150, 80);
  auto solidLayerRect = solidLayer->getBounds();
  EXPECT_TRUE(solidLayerRect == bounds);

  displayList->render(surface.get());
  context->submit();
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/draw_solid"));
  device->unlock();
}

TGFX_TEST(LayerTest, FilterTest) {
  auto filter = DropShadowFilter::Make(-80, -80, 0, 0, Color::Black());
  auto filter2 = DropShadowFilter::Make(-40, -40, 0, 0, Color::Green());
  auto filter3 = BlurFilter::Make(40, 40);
  auto image = MakeImage("resources/apitest/rotation.jpg");
  auto device = DevicePool::Make();
  auto context = device->lockContext();
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
  EXPECT_EQ(Rect::MakeLTRB(126.5f, 126.5f, 1725.5f, 2229.5f), bounds);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/filterTest"));
  device->unlock();
}

TGFX_TEST(LayerTest, filterClip) {
  auto filter = DropShadowFilter::Make(-10, -10, 0, 0, Color::Black());

  auto image = MakeImage("resources/apitest/rotation.jpg");
  auto device = DevicePool::Make();
  auto context = device->lockContext();
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
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/filterClip"));
  device->unlock();
}

TGFX_TEST(LayerTest, dropshadowLayerFilter) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/image_as_mask.png");
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
  auto filter2 = DropShadowFilter::Make(0, 0, 15, 15, Color::White(), true);
  layer2->setFilters({filter2});
  displayList->root()->addChild(layer2);

  auto layer3 = ImageLayer::Make();
  layer3->setImage(image);
  layer3->setMatrix(Matrix::MakeTrans(padding, imageWidth + padding * 2));
  auto filter3 = DropShadowFilter::Make(0, 0, 15, 15, Color::White());
  layer3->setFilters({filter3});
  displayList->root()->addChild(layer3);

  auto layer4 = ImageLayer::Make();
  layer4->setImage(image);
  layer4->setMatrix(Matrix::MakeTrans(imageWidth + padding * 2, imageWidth + padding * 2));
  auto filter4 = DropShadowFilter::Make(3, 3, 0, 0, Color::White());
  layer4->setFilters({filter4});
  displayList->root()->addChild(layer4);

  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/dropShadow"));
  device->unlock();

  auto src = Rect::MakeXYWH(10, 10, 10, 10);
  auto bounds = filter4->getImageFilter(1.0f)->filterBounds(src);
  EXPECT_EQ(bounds, Rect::MakeXYWH(10, 10, 13, 13));
  bounds = ImageFilter::DropShadowOnly(3, 3, 0, 0, Color::White())->filterBounds(src);
  EXPECT_EQ(bounds, Rect::MakeXYWH(13, 13, 10, 10));
}

TGFX_TEST(LayerTest, colorBlendLayerFilter) {

  auto device = DevicePool::Make();
  auto context = device->lockContext();
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
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/ModeColorFilter"));
  device->unlock();
}

TGFX_TEST(LayerTest, colorMatrixLayerFilter) {

  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
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
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/identityMatrix"));
  std::array<float, 20> greyColorMatrix = {0.21f, 0.72f, 0.07f, 0.41f, 0,  // red
                                           0.21f, 0.72f, 0.07f, 0.41f, 0,  // green
                                           0.21f, 0.72f, 0.07f, 0.41f, 0,  // blue
                                           0,     0,     0,     1.0f,  0};

  filter->setMatrix(greyColorMatrix);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/greyColorMatrix"));
  device->unlock();
}

TGFX_TEST(LayerTest, blurLayerFilter) {
  auto blur = BlurFilter::Make(130.f, 130.f);
  EXPECT_EQ(blur->blurrinessY(), 130.f);
  EXPECT_EQ(blur->blurrinessX(), 130.f);
  blur->setTileMode(TileMode::Clamp);
  EXPECT_EQ(blur->tileMode(), TileMode::Clamp);
  auto imageFilter = std::static_pointer_cast<BlurImageFilter>(blur->getImageFilter(0.5f));
  auto imageFilter2 =
      std::static_pointer_cast<BlurImageFilter>(ImageFilter::Blur(65.f, 65.f, TileMode::Clamp));
  EXPECT_EQ(imageFilter->blurOffset, imageFilter2->blurOffset);
  EXPECT_EQ(imageFilter->downScaling, imageFilter2->downScaling);
  EXPECT_EQ(imageFilter->tileMode, imageFilter2->tileMode);
  EXPECT_EQ(imageFilter->iteration, imageFilter2->iteration);

  EXPECT_EQ(blur->getImageFilter(0.5f)->filterBounds(Rect::MakeWH(200, 200)),
            imageFilter2->filterBounds(Rect::MakeWH(200, 200)));
}

TGFX_TEST(LayerTest, PassthroughAndNormal) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 800, 400);

  surface->getCanvas()->clearRect(Rect::MakeWH(800, 400), Color::FromRGBA(53, 53, 53));
  DisplayList displayList;

  auto root = ShapeLayer::Make();
  root->setMatrix(Matrix::MakeTrans(50, 50));
  Path rect;
  rect.addRect(0, 0, 200, 200);
  auto rect1 = ShapeLayer::Make();
  rect1->setPath(rect);
  rect1->setFillStyle(SolidColor::Make(Color::FromRGBA(123, 77, 77)));
  auto rect2 = ShapeLayer::Make();
  rect2->setPath(rect);
  rect2->setMatrix(Matrix::MakeTrans(100, 100));
  rect2->setFillStyle(SolidColor::Make(Color::FromRGBA(219, 32, 32)));
  root->addChild(rect1);
  root->addChild(rect2);
  displayList.root()->addChild(root);
  rect1->setBlendMode(BlendMode::SoftLight);
  rect2->setBlendMode(BlendMode::Screen);
  root->setShouldRasterize(true);
  displayList.render(surface.get(), false);

  root->setMatrix(Matrix::MakeTrans(400, 50));
  root->setShouldRasterize(false);
  displayList.render(surface.get(), false);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PassThoughAndNormal"));
  device->unlock();
}

TGFX_TEST(LayerTest, ContentVersion) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 100, 100);
  DisplayList displayList;
  auto shapeLayer = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeXYWH(0, 0, 100, 100));
  shapeLayer->setPath(path);
  shapeLayer->setFillStyle(SolidColor::Make(Color::FromRGBA(255, 0, 0)));
  displayList.root()->addChild(shapeLayer);
  auto contentVersion = surface->contentVersion();
  displayList.render(surface.get());
  EXPECT_NE(surface->contentVersion(), contentVersion);
  contentVersion = surface->contentVersion();
  displayList.render(surface.get());
  EXPECT_EQ(surface->contentVersion(), contentVersion);
  displayList.render(surface.get(), false);
  EXPECT_NE(surface->contentVersion(), contentVersion);
  contentVersion = surface->contentVersion();
  surface->getCanvas()->clear();
  EXPECT_NE(surface->contentVersion(), contentVersion);
  contentVersion = surface->contentVersion();
  displayList.render(surface.get());
  EXPECT_NE(surface->contentVersion(), contentVersion);
  contentVersion = surface->contentVersion();

  auto surface2 = Surface::Make(context, 100, 100);
  EXPECT_EQ(surface2->contentVersion(), 1u);
  displayList.render(surface2.get());
  EXPECT_NE(surface2->contentVersion(), 1u);
  displayList.render(surface.get());
  EXPECT_NE(surface->contentVersion(), contentVersion);
  device->unlock();
}

/**
 * The schematic diagram is as follows:
 * https://www.geogebra.org/graphing/et36u73x
 * https://codesign-1252678369.cos.ap-guangzhou.myqcloud.com/getLayersUnderPoint.png
 */
TGFX_TEST(LayerTest, getLayersUnderPoint) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto surface = Surface::Make(context, 800, 800);
  auto canvas = surface->getCanvas();
  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();
  rootLayer->setName("root_layer");
  displayList->root()->addChild(rootLayer);

  auto imageLayer = ImageLayer::Make();
  imageLayer->setName("image_layer");
  imageLayer->setMatrix(Matrix::MakeScale(3.0f, 3.0f));
  auto image = MakeImage("resources/apitest/image_as_mask.png");
  imageLayer->setImage(image);
  SamplingOptions options(FilterMode::Nearest, MipmapMode::None);
  imageLayer->setSampling(options);
  rootLayer->addChild(imageLayer);
  auto imageLayerBounds = imageLayer->getBounds();
  imageLayer->getGlobalMatrix().mapRect(&imageLayerBounds);
  printf("imageLayerBounds: (%f, %f, %f, %f)\n", imageLayerBounds.left, imageLayerBounds.top,
         imageLayerBounds.right, imageLayerBounds.bottom);

  auto shaperLayer = ShapeLayer::Make();
  shaperLayer->setName("shaper_layer");
  Path path = {};
  path.moveTo(100, 50);
  path.lineTo(150, 125);
  path.lineTo(50, 125);
  path.close();
  shaperLayer->setPath(path);
  auto fillStyle = SolidColor::Make(Color::FromRGBA(255, 0, 0, 127));
  shaperLayer->setFillStyle(fillStyle);
  shaperLayer->setMatrix(Matrix::MakeTrans(100.0f, 0.0f) * Matrix::MakeScale(2.0f, 2.0f));
  rootLayer->addChild(shaperLayer);
  auto shaperLayerBounds = shaperLayer->getBounds();
  shaperLayer->getGlobalMatrix().mapRect(&shaperLayerBounds);
  printf("shaperLayerBounds: (%f, %f, %f, %f)\n", shaperLayerBounds.left, shaperLayerBounds.top,
         shaperLayerBounds.right, shaperLayerBounds.bottom);

  auto textLayer = TextLayer::Make();
  textLayer->setName("text_layer");
  textLayer->setText("Hello World!");
  textLayer->setMatrix(Matrix::MakeTrans(50.0f, 50.0f) * Matrix::MakeScale(5.0f, 5.0f));
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  tgfx::Font font(typeface, 20);
  textLayer->setFont(font);
  rootLayer->addChild(textLayer);
  auto textLayerBounds = textLayer->getBounds();
  textLayer->getGlobalMatrix().mapRect(&textLayerBounds);
  printf("textLayerBounds: (%f, %f, %f, %f)\n", textLayerBounds.left, textLayerBounds.top,
         textLayerBounds.right, textLayerBounds.bottom);

  auto shaperLayer2 = ShapeLayer::Make();
  shaperLayer2->setName("shaper_layer2");
  shaperLayer2->setMatrix(Matrix::MakeTrans(550.0f, 150.0f) * Matrix::MakeRotate(45.0f));
  auto rect2 = Rect::MakeXYWH(0, 0, 80, 80);
  Path path2 = {};
  path2.addRect(rect2);
  shaperLayer2->setPath(path2);
  auto fillStyle2 = SolidColor::Make(Color::FromRGBA(175, 27, 193, 255));
  shaperLayer2->setFillStyle(fillStyle2);
  rootLayer->addChild(shaperLayer2);
  auto shaperLayer2Bounds = shaperLayer2->getBounds();
  shaperLayer2->getGlobalMatrix().mapRect(&shaperLayer2Bounds);
  printf("shaperLayer2Bounds: (%f, %f, %f, %f)\n", shaperLayer2Bounds.left, shaperLayer2Bounds.top,
         shaperLayer2Bounds.right, shaperLayer2Bounds.bottom);

  auto rootLayerBounds = rootLayer->getBounds();
  printf("rootLayerBounds: (%f, %f, %f, %f)\n", rootLayerBounds.left, rootLayerBounds.top,
         rootLayerBounds.right, rootLayerBounds.bottom);

  displayList->render(surface.get());

  auto paint = Paint();
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(1.0f);
  paint.setColor(Color::Green());
  canvas->drawRect(imageLayerBounds, paint);
  canvas->drawRect(shaperLayerBounds, paint);
  canvas->drawRect(textLayerBounds, paint);
  canvas->drawRect(shaperLayer2Bounds, paint);
  paint.setColor(Color::Red());
  canvas->drawRect(rootLayerBounds, paint);

  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  // P1(200, 100) is in the text_layer, shaper_layer, image_layer, root_layer
  auto layers = rootLayer->getLayersUnderPoint(200.0f, 100.0f);
  canvas->drawCircle(200.0f, 100.0f, 5.0f, paint);
  printf("layers.size(): %zu\n", layers.size());
  std::string layerNameJoin = "";
  for (auto layer : layers) {
    printf("layer: %s\n", layer->name().c_str());
    layerNameJoin += layer->name() + "|";
  }
  printf("\n");
  EXPECT_EQ(static_cast<int>(layers.size()), 4);
  EXPECT_EQ(layerNameJoin, "text_layer|shaper_layer|image_layer|root_layer|");

  // P2(330, 130) is in the text_layer, shaper_layer, root_layer
  layerNameJoin = "";
  layers = rootLayer->getLayersUnderPoint(330.0f, 130.0f);
  canvas->drawCircle(330.0f, 130.0f, 5.0f, paint);
  printf("layers.size(): %zu\n", layers.size());
  for (auto layer : layers) {
    printf("layer: %s\n", layer->name().c_str());
    layerNameJoin += layer->name() + "|";
  }
  printf("\n");
  EXPECT_EQ(static_cast<int>(layers.size()), 3);
  EXPECT_EQ(layerNameJoin, "text_layer|shaper_layer|root_layer|");

  // P3(369.4903917863642, 119.382137866799) is in the text_layer, shaper_layer, root_layer
  layerNameJoin = "";
  layers = rootLayer->getLayersUnderPoint(369.4903917863642f, 119.382137866799f);
  canvas->drawCircle(369.4903917863642f, 119.382137866799f, 5.0f, paint);
  printf("layers.size(): %zu\n", layers.size());
  for (auto layer : layers) {
    printf("layer: %s\n", layer->name().c_str());
    layerNameJoin += layer->name() + "|";
  }
  printf("\n");
  EXPECT_EQ(static_cast<int>(layers.size()), 3);
  EXPECT_EQ(layerNameJoin, "text_layer|shaper_layer|root_layer|");

  // P4(376.3366070606341, 226.8150544784194) is in the shaper_layer, root_layer
  layerNameJoin = "";
  layers = rootLayer->getLayersUnderPoint(376.3366070606341f, 226.8150544784194f);
  canvas->drawCircle(376.3366070606341f, 226.8150544784194f, 5.0f, paint);
  printf("layers.size(): %zu\n", layers.size());
  for (auto layer : layers) {
    printf("layer: %s\n", layer->name().c_str());
    layerNameJoin += layer->name() + "|";
  }
  printf("\n");
  EXPECT_EQ(static_cast<int>(layers.size()), 2);
  EXPECT_EQ(layerNameJoin, "shaper_layer|root_layer|");

  // P5(538.0126139222378, 91.4706448255447) is in the text_layer, root_layer
  layerNameJoin = "";
  layers = rootLayer->getLayersUnderPoint(538.0126139222378f, 91.4706448255447f);
  canvas->drawCircle(538.0126139222378f, 91.4706448255447f, 5.0f, paint);
  printf("layers.size(): %zu\n", layers.size());
  for (auto layer : layers) {
    printf("layer: %s\n", layer->name().c_str());
    layerNameJoin += layer->name() + "|";
  }
  printf("\n");
  EXPECT_EQ(static_cast<int>(layers.size()), 2);
  EXPECT_EQ(layerNameJoin, "text_layer|root_layer|");

  // P6(526.4267111503966, 279.4782488958804) is in the root_layer
  layerNameJoin = "";
  layers = rootLayer->getLayersUnderPoint(526.4267111503966f, 279.4782488958804f);
  canvas->drawCircle(526.4267111503966f, 279.4782488958804f, 5.0f, paint);
  printf("layers.size(): %zu\n", layers.size());
  for (auto layer : layers) {
    printf("layer: %s\n", layer->name().c_str());
    layerNameJoin += layer->name() + "|";
  }
  printf("\n");
  EXPECT_EQ(static_cast<int>(layers.size()), 0);
  EXPECT_EQ(layerNameJoin, "");

  // P7(686.0488534297194, 375.2199363468245) is out of the root_layer
  layerNameJoin = "";
  layers = rootLayer->getLayersUnderPoint(686.0488534297194f, 375.2199363468245f);
  canvas->drawCircle(686.0488534297194f, 375.2199363468245f, 5.0f, paint);
  printf("layers.size(): %zu\n", layers.size());
  for (auto layer : layers) {
    printf("layer: %s\n", layer->name().c_str());
    layerNameJoin += layer->name() + "|";
  }
  printf("\n");
  EXPECT_EQ(static_cast<int>(layers.size()), 0);
  EXPECT_EQ(layerNameJoin, "");

  // P8(-64.7176461855979, 83.8344816350128) is out of the root_layer
  layerNameJoin = "";
  layers = rootLayer->getLayersUnderPoint(-64.7176461855979f, 83.8344816350128f);
  canvas->drawCircle(-64.7176461855979f, 83.8344816350128f, 5.0f, paint);
  printf("layers.size(): %zu\n", layers.size());
  for (auto layer : layers) {
    printf("layer: %s\n", layer->name().c_str());
    layerNameJoin += layer->name() + "|";
  }
  printf("\n");
  EXPECT_EQ(static_cast<int>(layers.size()), 0);
  EXPECT_EQ(layerNameJoin, "");

  // P9(50, 300) is in the image_layer, root_layer
  layerNameJoin = "";
  layers = rootLayer->getLayersUnderPoint(50.0f, 300.0f);
  canvas->drawCircle(50.0f, 300.0f, 5.0f, paint);
  printf("layers.size(): %zu\n", layers.size());
  for (auto layer : layers) {
    printf("layer: %s\n", layer->name().c_str());
    layerNameJoin += layer->name() + "|";
  }
  printf("\n");
  EXPECT_EQ(static_cast<int>(layers.size()), 2);
  EXPECT_EQ(layerNameJoin, "image_layer|root_layer|");

  // P10(511.6931040682015, 171.034333482391) is in the shaper_layer2, root_layer
  layerNameJoin = "";
  layers = rootLayer->getLayersUnderPoint(511.6931040682015f, 171.034333482391f);
  canvas->drawCircle(511.6931040682015f, 171.034333482391f, 5.0f, paint);
  printf("layers.size(): %zu\n", layers.size());
  for (auto layer : layers) {
    printf("layer: %s\n", layer->name().c_str());
    layerNameJoin += layer->name() + "|";
  }
  printf("\n");
  EXPECT_EQ(static_cast<int>(layers.size()), 0);
  EXPECT_EQ(layerNameJoin, "");

  // P11(540, 200) is in the shaper_layer2, root_layer
  layerNameJoin = "";
  layers = rootLayer->getLayersUnderPoint(540.0f, 200.0f);
  canvas->drawCircle(540.0f, 200.0f, 5.0f, paint);
  printf("layers.size(): %zu\n", layers.size());
  for (auto layer : layers) {
    printf("layer: %s\n", layer->name().c_str());
    layerNameJoin += layer->name() + "|";
  }
  printf("\n");
  EXPECT_EQ(static_cast<int>(layers.size()), 2);
  EXPECT_EQ(layerNameJoin, "shaper_layer2|root_layer|");

  context->submit();
  Baseline::Compare(surface, "LayerTest/getLayersUnderPoint");
  device->unlock();
}

/**
 * The schematic diagram is as follows(Visit geogebra online vector map to view pixel details):
 * https://www.geogebra.org/classic/krbzbz6m
 * https://codesign-1252678369.cos.ap-guangzhou.myqcloud.com/hitTestPoint.png
 * https://codesign-1252678369.cos.ap-guangzhou.myqcloud.com/Layer_hitTestPoint.png
 */
TGFX_TEST(LayerTest, hitTestPoint) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto surface = Surface::Make(context, 800, 800);
  auto canvas = surface->getCanvas();
  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();
  rootLayer->setName("root_layer");
  displayList->root()->addChild(rootLayer);

  auto shaperLayer1 = ShapeLayer::Make();
  shaperLayer1->setName("shaper_layer1");
  Path path1 = {};
  path1.moveTo(100, 50);
  path1.lineTo(150, 125);
  path1.lineTo(50, 125);
  path1.close();
  shaperLayer1->setPath(path1);
  auto fillStyle1 = SolidColor::Make(Color::FromRGBA(255, 0, 0, 127));
  shaperLayer1->setFillStyle(fillStyle1);
  shaperLayer1->setMatrix(Matrix::MakeTrans(100.0f, 50.0f));
  rootLayer->addChild(shaperLayer1);
  auto shaperLayer1Bounds = shaperLayer1->getBounds();
  shaperLayer1->getGlobalMatrix().mapRect(&shaperLayer1Bounds);
  printf("shaperLayer1Bounds: (%f, %f, %f, %f)\n", shaperLayer1Bounds.left, shaperLayer1Bounds.top,
         shaperLayer1Bounds.right, shaperLayer1Bounds.bottom);

  auto shaperLayer2 = ShapeLayer::Make();
  shaperLayer2->setName("shaper_layer2");
  Rect rect = Rect::MakeLTRB(220, 100, 370, 250);
  Path path2 = {};
  path2.addOval(rect);
  path2.close();
  shaperLayer2->setPath(path2);
  auto fillStyle2 = SolidColor::Make(Color::FromRGBA(127, 255, 0, 127));
  shaperLayer2->setFillStyle(fillStyle2);
  rootLayer->addChild(shaperLayer2);
  auto shaperLayer2Bounds = shaperLayer2->getBounds();
  shaperLayer2->getGlobalMatrix().mapRect(&shaperLayer2Bounds);
  printf("shaperLayer2Bounds: (%f, %f, %f, %f)\n", shaperLayer2Bounds.left, shaperLayer2Bounds.top,
         shaperLayer2Bounds.right, shaperLayer2Bounds.bottom);

  auto rootLayerBounds = rootLayer->getBounds();
  rootLayerBounds.roundOut();
  printf("rootLayerBounds: (%f, %f, %f, %f)\n", rootLayerBounds.left, rootLayerBounds.top,
         rootLayerBounds.right, rootLayerBounds.bottom);

  displayList->render(surface.get());

  auto paint = Paint();
  paint.setColor(Color::Red());
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(1.0f);
  canvas->drawRect(rootLayerBounds, paint);

  // P1(160, 120)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point p1 = {160.0f, 120.0f};
  canvas->drawCircle(p1.x, p1.y, 1.0f, paint);
  EXPECT_EQ(true, shaperLayer1->hitTestPoint(p1.x, p1.y));
  EXPECT_EQ(false, shaperLayer1->hitTestPoint(p1.x, p1.y, true));

  // P2(186.66668f, 120.0f)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point p2 = {186.66668f, 120.0f};
  canvas->drawCircle(p2.x, p2.y, 1.0f, paint);
  EXPECT_EQ(true, shaperLayer1->hitTestPoint(p2.x, p2.y));
  EXPECT_EQ(true, shaperLayer1->hitTestPoint(p2.x, p2.y, true));

  // P3(172.0774145878251, 140)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point p3 = {172.0774145878251f, 140.0f};
  canvas->drawCircle(p3.x, p3.y, 1.0f, paint);
  EXPECT_EQ(true, shaperLayer1->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(false, shaperLayer1->hitTestPoint(p3.x, p3.y, true));

  // P4(200, 150)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point p4 = {200.0f, 150.0f};
  canvas->drawCircle(p4.x, p4.y, 1.0f, paint);
  EXPECT_EQ(true, shaperLayer1->hitTestPoint(p4.x, p4.y));
  EXPECT_EQ(true, shaperLayer1->hitTestPoint(p4.x, p4.y, true));

  // P5(225, 120)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point p5 = {225.0f, 120.0f};
  canvas->drawCircle(p5.x, p5.y, 1.0f, paint);
  EXPECT_EQ(true, shaperLayer1->hitTestPoint(p5.x, p5.y));
  EXPECT_EQ(false, shaperLayer1->hitTestPoint(p5.x, p5.y, true));

  // P6(200, 180)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point p6 = {200.0f, 180.0f};
  canvas->drawCircle(p6.x, p6.y, 1.0f, paint);
  EXPECT_EQ(false, shaperLayer1->hitTestPoint(p6.x, p6.y));
  EXPECT_EQ(false, shaperLayer1->hitTestPoint(p6.x, p6.y, true));

  // Q1(227.79885, -141.69835)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point q1 = {227.79885f, 141.69835f};
  canvas->drawCircle(q1.x, q1.y, 1.0f, paint);
  EXPECT_EQ(true, shaperLayer1->hitTestPoint(q1.x, q1.y));
  EXPECT_EQ(true, shaperLayer1->hitTestPoint(q1.x, q1.y, true));
  EXPECT_EQ(true, shaperLayer2->hitTestPoint(q1.x, q1.y));
  EXPECT_EQ(true, shaperLayer2->hitTestPoint(q1.x, q1.y, true));

  // Q2(230.0, 160.0)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point q2 = {230.0f, 160.0f};
  canvas->drawCircle(q2.x, q2.y, 1.0f, paint);
  EXPECT_EQ(true, shaperLayer1->hitTestPoint(q2.x, q2.y));
  EXPECT_EQ(true, shaperLayer1->hitTestPoint(q2.x, q2.y, true));
  EXPECT_EQ(true, shaperLayer2->hitTestPoint(q2.x, q2.y));
  EXPECT_EQ(true, shaperLayer2->hitTestPoint(q2.x, q2.y, true));

  // Q3(270.0, 190.0)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point q3 = {270.0f, 190.0f};
  canvas->drawCircle(q3.x, q3.y, 1.0f, paint);
  EXPECT_EQ(false, shaperLayer1->hitTestPoint(q3.x, q3.y));
  EXPECT_EQ(false, shaperLayer1->hitTestPoint(q3.x, q3.y, true));
  EXPECT_EQ(true, shaperLayer2->hitTestPoint(q3.x, q3.y));
  EXPECT_EQ(true, shaperLayer2->hitTestPoint(q3.x, q3.y, true));

  // Q4(336.0, 239.0)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point q4 = {336.0f, 239.0f};
  canvas->drawCircle(q4.x, q4.y, 1.0f, paint);
  EXPECT_EQ(false, shaperLayer1->hitTestPoint(q4.x, q4.y));
  EXPECT_EQ(false, shaperLayer1->hitTestPoint(q4.x, q4.y, true));
  EXPECT_EQ(true, shaperLayer2->hitTestPoint(q4.x, q4.y));
  EXPECT_EQ(false, shaperLayer2->hitTestPoint(q4.x, q4.y, true));

  // Q5(240.0, 150.0)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point q5 = {240.0f, 150.0f};
  canvas->drawCircle(q5.x, q5.y, 1.0f, paint);
  EXPECT_EQ(true, shaperLayer1->hitTestPoint(q5.x, q5.y));
  EXPECT_EQ(false, shaperLayer1->hitTestPoint(q5.x, q5.y, true));
  EXPECT_EQ(true, shaperLayer2->hitTestPoint(q5.x, q5.y));
  EXPECT_EQ(true, shaperLayer2->hitTestPoint(q5.x, q5.y, true));

  context->submit();
  Baseline::Compare(surface, "LayerTest/Layer_hitTestPoint");
  device->unlock();
}

TGFX_TEST(LayerTest, hitTestPointNested) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto surface = Surface::Make(context, 800, 800);
  auto canvas = surface->getCanvas();
  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();
  rootLayer->setName("root_layer");
  displayList->root()->addChild(rootLayer);

  auto parentLayer = Layer::Make();
  parentLayer->setName("parent_layer");
  parentLayer->setMatrix(Matrix::MakeTrans(50.0f, 50.0f));
  auto imageLayer = ImageLayer::Make();
  imageLayer->setName("image_layer");
  auto image = MakeImage("resources/apitest/image_as_mask.png");
  imageLayer->setImage(image);
  SamplingOptions options(FilterMode::Nearest, MipmapMode::None);
  imageLayer->setSampling(options);
  imageLayer->setMatrix(Matrix::MakeScale(3.0f));
  parentLayer->addChild(imageLayer);
  rootLayer->addChild(parentLayer);
  auto imageLayerBounds = imageLayer->getBounds();
  imageLayer->getGlobalMatrix().mapRect(&imageLayerBounds);
  printf("imageLayerBounds: (%f, %f, %f, %f)\n", imageLayerBounds.left, imageLayerBounds.top,
         imageLayerBounds.right, imageLayerBounds.bottom);

  auto childLayer = Layer::Make();
  childLayer->setName("child_layer");
  childLayer->setMatrix(Matrix::MakeTrans(50.0f, 50.0f));
  auto shaperLayer = ShapeLayer::Make();
  shaperLayer->setName("shaper_layer");
  Rect rect = Rect::MakeLTRB(150, 150, 370, 370);
  Path path = {};
  path.addRect(rect);
  path.close();
  shaperLayer->setPath(path);
  auto fillStyle = SolidColor::Make(Color::FromRGBA(127, 255, 0, 127));
  shaperLayer->setFillStyle(fillStyle);
  childLayer->addChild(shaperLayer);
  parentLayer->addChild(childLayer);
  auto shaperLayerBounds = shaperLayer->getBounds();
  shaperLayer->getGlobalMatrix().mapRect(&shaperLayerBounds);
  printf("shaperLayerBounds: (%f, %f, %f, %f)\n", shaperLayerBounds.left, shaperLayerBounds.top,
         shaperLayerBounds.right, shaperLayerBounds.bottom);

  auto grandsonLayer = Layer::Make();
  grandsonLayer->setName("grandson_layer");
  grandsonLayer->setMatrix(Matrix::MakeTrans(50.0f, 50.0f));
  auto textLayer = TextLayer::Make();
  textLayer->setName("text_layer");
  textLayer->setText("Hello World!");
  textLayer->setMatrix(Matrix::MakeTrans(50.0f, -50.0f) * Matrix::MakeRotate(45.0f) *
                       Matrix::MakeScale(5.0f, 5.0f));
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  tgfx::Font font(typeface, 20);
  textLayer->setFont(font);
  grandsonLayer->addChild(textLayer);
  childLayer->addChild(grandsonLayer);
  auto textLayerBounds = textLayer->getBounds();
  textLayer->getGlobalMatrix().mapRect(&textLayerBounds);
  printf("textLayerBounds: (%f, %f, %f, %f)\n", textLayerBounds.left, textLayerBounds.top,
         textLayerBounds.right, textLayerBounds.bottom);

  auto rootLayerBounds = rootLayer->getBounds();
  rootLayer->getGlobalMatrix().mapRect(&rootLayerBounds);
  printf("rootLayerBounds: (%f, %f, %f, %f)\n", rootLayerBounds.left, rootLayerBounds.top,
         rootLayerBounds.right, rootLayerBounds.bottom);

  displayList->render(surface.get());

  auto paint = Paint();
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(1.0f);
  paint.setColor(Color::Red());
  canvas->drawRect(imageLayerBounds, paint);
  canvas->drawRect(shaperLayerBounds, paint);
  canvas->drawRect(textLayerBounds, paint);
  paint.setColor(Color::Green());
  canvas->drawRect(rootLayerBounds, paint);

  // P0(340.0, 340.0)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point p0 = {340.0f, 340.0f};
  canvas->drawCircle(p0.x, p0.y, 2.0f, paint);
  EXPECT_EQ(true, textLayer->hitTestPoint(p0.x, p0.y));
  EXPECT_EQ(false, textLayer->hitTestPoint(p0.x, p0.y, true));
  EXPECT_EQ(true, shaperLayer->hitTestPoint(p0.x, p0.y));
  EXPECT_EQ(true, shaperLayer->hitTestPoint(p0.x, p0.y, true));
  EXPECT_EQ(true, imageLayer->hitTestPoint(p0.x, p0.y));
  EXPECT_EQ(true, imageLayer->hitTestPoint(p0.x, p0.y, true));
  EXPECT_EQ(true, parentLayer->hitTestPoint(p0.x, p0.y));
  EXPECT_EQ(true, parentLayer->hitTestPoint(p0.x, p0.y, true));
  EXPECT_EQ(true, childLayer->hitTestPoint(p0.x, p0.y));
  EXPECT_EQ(true, childLayer->hitTestPoint(p0.x, p0.y, true));
  EXPECT_EQ(true, grandsonLayer->hitTestPoint(p0.x, p0.y));
  EXPECT_EQ(false, grandsonLayer->hitTestPoint(p0.x, p0.y, true));
  EXPECT_EQ(true, rootLayer->hitTestPoint(p0.x, p0.y));
  EXPECT_EQ(true, rootLayer->hitTestPoint(p0.x, p0.y, true));

  // P1(320.0, 320.0)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point p1 = {320.0f, 320.0f};
  canvas->drawCircle(p1.x, p1.y, 2.0f, paint);
  EXPECT_EQ(true, textLayer->hitTestPoint(p1.x, p1.y));
  EXPECT_EQ(true, textLayer->hitTestPoint(p1.x, p1.y, true));
  EXPECT_EQ(true, shaperLayer->hitTestPoint(p1.x, p1.y));
  EXPECT_EQ(true, shaperLayer->hitTestPoint(p1.x, p1.y, true));
  EXPECT_EQ(true, imageLayer->hitTestPoint(p1.x, p1.y));
  EXPECT_EQ(true, imageLayer->hitTestPoint(p1.x, p1.y, true));
  EXPECT_EQ(true, parentLayer->hitTestPoint(p1.x, p1.y));
  EXPECT_EQ(true, parentLayer->hitTestPoint(p1.x, p1.y, true));
  EXPECT_EQ(true, childLayer->hitTestPoint(p1.x, p1.y));
  EXPECT_EQ(true, childLayer->hitTestPoint(p1.x, p1.y, true));
  EXPECT_EQ(true, grandsonLayer->hitTestPoint(p1.x, p1.y));
  EXPECT_EQ(true, grandsonLayer->hitTestPoint(p1.x, p1.y, true));
  EXPECT_EQ(true, rootLayer->hitTestPoint(p1.x, p1.y));
  EXPECT_EQ(true, rootLayer->hitTestPoint(p1.x, p1.y, true));

  // P2(180.0, 140.0)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point p2 = {180.0f, 140.0f};
  canvas->drawCircle(p2.x, p2.y, 2.0f, paint);
  EXPECT_EQ(true, textLayer->hitTestPoint(p2.x, p2.y));
  EXPECT_EQ(true, textLayer->hitTestPoint(p2.x, p2.y, true));
  EXPECT_EQ(false, shaperLayer->hitTestPoint(p2.x, p2.y));
  EXPECT_EQ(false, shaperLayer->hitTestPoint(p2.x, p2.y, true));
  EXPECT_EQ(true, imageLayer->hitTestPoint(p2.x, p2.y));
  EXPECT_EQ(true, imageLayer->hitTestPoint(p2.x, p2.y, true));
  EXPECT_EQ(true, parentLayer->hitTestPoint(p2.x, p2.y));
  EXPECT_EQ(true, parentLayer->hitTestPoint(p2.x, p2.y, true));
  EXPECT_EQ(true, childLayer->hitTestPoint(p2.x, p2.y));
  EXPECT_EQ(true, childLayer->hitTestPoint(p2.x, p2.y, true));
  EXPECT_EQ(true, grandsonLayer->hitTestPoint(p2.x, p2.y));
  EXPECT_EQ(true, grandsonLayer->hitTestPoint(p2.x, p2.y, true));
  EXPECT_EQ(true, rootLayer->hitTestPoint(p2.x, p2.y));
  EXPECT_EQ(true, rootLayer->hitTestPoint(p2.x, p2.y, true));

  // P3(80.0, 80.0)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point p3 = {80.0f, 80.0f};
  canvas->drawCircle(p3.x, p3.y, 2.0f, paint);
  EXPECT_EQ(false, textLayer->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(false, textLayer->hitTestPoint(p3.x, p3.y, true));
  EXPECT_EQ(false, shaperLayer->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(false, shaperLayer->hitTestPoint(p3.x, p3.y, true));
  EXPECT_EQ(true, imageLayer->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(true, imageLayer->hitTestPoint(p3.x, p3.y, true));
  EXPECT_EQ(true, parentLayer->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(true, parentLayer->hitTestPoint(p3.x, p3.y, true));
  EXPECT_EQ(false, childLayer->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(false, childLayer->hitTestPoint(p3.x, p3.y, true));
  EXPECT_EQ(false, grandsonLayer->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(false, grandsonLayer->hitTestPoint(p3.x, p3.y, true));
  EXPECT_EQ(true, rootLayer->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(true, rootLayer->hitTestPoint(p3.x, p3.y, true));

  context->submit();
  Baseline::Compare(surface, "LayerTest/Layer_hitTestPointNested");
  device->unlock();
}
}  // namespace tgfx
