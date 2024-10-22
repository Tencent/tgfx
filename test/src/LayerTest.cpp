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
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/filters/BlurLayerFilter.h"
#include "tgfx/layers/filters/ColorBlendLayerFilter.h"
#include "tgfx/layers/filters/ColorMatrixLayerFilter.h"
#include "tgfx/layers/filters/DropShadowLayerFilter.h"
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

TGFX_TEST(LayerTest, FilterTest) {
  auto filter = DropShadowLayerFilter::Make();
  filter->setDx(-80);
  filter->setDy(-80);
  auto filter2 = DropShadowLayerFilter::Make();
  filter2->setDx(-40);
  filter2->setDy(-40);
  filter2->setColor(Color::Green());
  auto filter3 = BlurLayerFilter::Make();
  filter3->setBlurrinessX(40);
  filter3->setBlurrinessY(40);
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
  EXPECT_EQ(Rect::MakeLTRB(186.5f, 186.5f, 1725.5f, 2229.5f), bounds);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/filterTest"));
  device->unlock();
}

TGFX_TEST(LayerTest, filterClip) {
  auto filter = DropShadowLayerFilter::Make();
  filter->setDx(-10);
  filter->setDy(-10);
  filter->setClipBounds(Rect::MakeLTRB(-20, -20, 200, 200));
  filter->setColor(Color::Black());
  filter->setDropsShadowOnly(false);

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
  EXPECT_EQ(Rect::MakeLTRB(45.f, 45.f, 150.f, 150.f), bounds);
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
  auto filter = BlurLayerFilter::Make();
  filter->setBlurrinessX(15);
  filter->setBlurrinessY(15);
  auto layer = ImageLayer::Make();
  layer->setImage(image);
  layer->setMatrix(Matrix::MakeTrans(padding, padding));
  layer->setFilters({filter});
  auto displayList = std::make_unique<DisplayList>();
  displayList->root()->addChild(layer);

  auto layer2 = ImageLayer::Make();
  layer2->setImage(image);
  layer2->setMatrix(Matrix::MakeTrans(imageWidth + padding * 2, padding));
  auto filter2 = DropShadowLayerFilter::Make();
  filter2->setBlurrinessX(15);
  filter2->setBlurrinessY(15);
  filter2->setColor(Color::White());
  filter2->setDropsShadowOnly(true);
  layer2->setFilters({filter2});
  displayList->root()->addChild(layer2);

  auto layer3 = ImageLayer::Make();
  layer3->setImage(image);
  layer3->setMatrix(Matrix::MakeTrans(padding, imageWidth + padding * 2));
  auto filter3 = DropShadowLayerFilter::Make();
  filter3->setBlurrinessX(15);
  filter3->setBlurrinessY(15);
  filter3->setColor(Color::White());
  layer3->setFilters({filter3});
  displayList->root()->addChild(layer3);

  auto layer4 = ImageLayer::Make();
  layer4->setImage(image);
  layer4->setMatrix(Matrix::MakeTrans(imageWidth + padding * 2, imageWidth + padding * 2));
  auto filter4 = DropShadowLayerFilter::Make();
  filter4->setDx(3);
  filter4->setDy(3);
  filter4->setColor(Color::White());
  layer4->setFilters({filter4});
  displayList->root()->addChild(layer4);

  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/dropShadow"));
  device->unlock();

  auto src = Rect::MakeXYWH(10, 10, 10, 10);
  auto bounds = filter4->filterBounds(src);
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

  auto filter = ColorBlendLayerFilter::Make();

  filter->setColor(Color::Red());
  filter->setBlendMode(BlendMode::Multiply);
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
  auto filter = ColorMatrixLayerFilter::Make();
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
  layer->setFilters({filter});
  filter->setMatrix(matrix);
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
  auto blur = BlurLayerFilter::Make();
  blur->setBlurrinessY(130.f);
  EXPECT_EQ(blur->blurrinessY(), 130.f);
  blur->setBlurrinessX(130.f);
  EXPECT_EQ(blur->blurrinessX(), 130.f);
  blur->setTileMode(TileMode::Clamp);
  EXPECT_EQ(blur->tileMode(), TileMode::Clamp);
  auto clipRect = Rect::MakeLTRB(0, 0, 100, 100);
  blur->setClipBounds(clipRect);
  EXPECT_EQ(blur->clipBounds(), Rect::MakeLTRB(0, 0, 100, 100));
  auto imageFilter = std::static_pointer_cast<BlurImageFilter>(blur->getImageFilter(0.5f));
  auto imageFilter2 =
      std::static_pointer_cast<BlurImageFilter>(ImageFilter::Blur(65.f, 65.f, TileMode::Clamp));
  EXPECT_EQ(imageFilter->blurOffset, imageFilter2->blurOffset);
  EXPECT_EQ(imageFilter->downScaling, imageFilter2->downScaling);
  EXPECT_EQ(imageFilter->tileMode, imageFilter2->tileMode);
  EXPECT_EQ(imageFilter->iteration, imageFilter2->iteration);

  Rect rect;
  clipRect.scale(0.5, 0.5);
  imageFilter2->applyCropRect(Rect::MakeWH(200, 200), &rect, &clipRect);
  EXPECT_EQ(blur->filterBounds(Rect::MakeWH(200, 200), 0.5f), rect);
}
}  // namespace tgfx
