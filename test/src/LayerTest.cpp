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

#include <vector>
#include <math.h>
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/TextLayer.h"
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
  EXPECT_EQ(parent->owner(), nullptr);
  EXPECT_EQ(child1->owner(), nullptr);
  EXPECT_EQ(child2->owner(), nullptr);
  EXPECT_EQ(child3->owner(), nullptr);
  displayList->root()->addChild(parent);
  EXPECT_EQ(parent->owner(), displayList.get());
  EXPECT_EQ(child1->owner(), displayList.get());
  EXPECT_EQ(child2->owner(), displayList.get());
  EXPECT_EQ(child3->owner(), displayList.get());

  parent->removeFromParent();
  EXPECT_EQ(parent->owner(), nullptr);
  EXPECT_EQ(child1->owner(), nullptr);
  EXPECT_EQ(child2->owner(), nullptr);
  EXPECT_EQ(child3->owner(), nullptr);
  displayList->root()->addChild(parent);

  // Test replacing a child
  auto replacedChild = Layer::Make();
  auto replacedChild2 = Layer::Make();
  parent->replaceChild(replacedChild, replacedChild2);
  EXPECT_EQ(replacedChild2->parent(), nullptr);
  EXPECT_EQ(replacedChild2->owner(), nullptr);

  parent->replaceChild(child1, replacedChild);
  EXPECT_EQ(replacedChild->parent(), parent);
  EXPECT_EQ(replacedChild->owner(), displayList.get());
  EXPECT_FALSE(parent->contains(child1));
  EXPECT_FALSE(parent->contains(child2));
  EXPECT_TRUE(parent->contains(replacedChild));
  EXPECT_TRUE(parent->children().size() == 2);
  EXPECT_EQ(parent->getChildIndex(replacedChild), 1);
  parent->replaceChild(replacedChild, child2);
  EXPECT_EQ(child2->parent(), parent);
  EXPECT_EQ(child2->owner(), displayList.get());
  EXPECT_FALSE(parent->contains(replacedChild));
  EXPECT_TRUE(parent->contains(child2));
  EXPECT_TRUE(child1->children().size() == 0);
  parent->addChildAt(child1, 1);

  // Test removing a child
  auto removedChild = parent->removeChildAt(0);
  EXPECT_EQ(removedChild, child3);
  EXPECT_EQ(child3->parent(), nullptr);
  EXPECT_EQ(child3->owner(), nullptr);
  EXPECT_FALSE(parent->contains(child3));
  EXPECT_TRUE(parent->children().size() == 2);
  EXPECT_EQ(parent->getChildIndex(child1), 0);
  EXPECT_EQ(parent->getChildIndex(child2), 1);

  // Test removing all children
  parent->removeChildren();
  EXPECT_EQ(child2->parent(), nullptr);
  EXPECT_EQ(child2->owner(), nullptr);
  EXPECT_FALSE(parent->contains(child2));
  EXPECT_EQ(child2->parent(), nullptr);
  EXPECT_EQ(child2->owner(), nullptr);
  EXPECT_FALSE(parent->contains(child1));
  EXPECT_EQ(child1->parent(), nullptr);
  EXPECT_EQ(child1->owner(), nullptr);
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
  auto canvas = surface->getCanvas();
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
  displayList->draw(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/draw_text"));
  device->unlock();
}

TGFX_TEST(LayerTest, imageLayer) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto image = MakeImage("resources/apitest/image_as_mask.png");
  auto surface = Surface::Make(context, image->width() * 5, image->height() * 5);
  auto canvas = surface->getCanvas();
  auto displayList = std::make_unique<DisplayList>();
  auto layer = Layer::Make();
  displayList->root()->addChild(layer);
  auto imageLayer = ImageLayer::Make();
  layer->addChild(imageLayer);
  imageLayer->setImage(image);
  SamplingOptions options(FilterMode::Nearest, MipmapMode::None);
  imageLayer->setSampling(options);
  imageLayer->setMatrix(Matrix::MakeScale(5.0f));
  displayList->draw(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/imageLayer"));
  device->unlock();
}

TGFX_TEST(LayerTest, Layer_getTotalMatrix) {
  auto parent = Layer::Make();
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

  auto greatGrandsonTotalMatrix = greatGrandson->getTotalMatrix();
  EXPECT_EQ(greatGrandsonTotalMatrix, Matrix::MakeTrans(40, 40));

  EXPECT_EQ(greatGrandson->matrix(), Matrix::MakeTrans(10, 10));
  EXPECT_EQ(grandChild->matrix(), Matrix::MakeTrans(10, 10));
  EXPECT_EQ(child->matrix(), Matrix::MakeTrans(10, 10));
  EXPECT_EQ(parent->matrix(), Matrix::MakeTrans(10, 10));

  auto rotateMat = Matrix::MakeRotate(45);
  greatGrandson->setMatrix(rotateMat * greatGrandson->matrix());
  EXPECT_EQ(greatGrandson->matrix().getTranslateX(), 0.0f);
  EXPECT_EQ(greatGrandson->matrix().getTranslateY(), 10.0f * std::sqrt(2.0f));

  greatGrandsonTotalMatrix = greatGrandson->getTotalMatrix();
  auto grandChildTotalMatrix = grandChild->getTotalMatrix();
  EXPECT_FLOAT_EQ(greatGrandsonTotalMatrix.getTranslateX(), grandChildTotalMatrix.getTranslateX());
  EXPECT_FLOAT_EQ(greatGrandsonTotalMatrix.getTranslateY(), grandChildTotalMatrix.getTranslateX() + 10.0f * std::sqrt(2.0f));
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
  layerA3->setMatrix(Matrix::MakeTrans(10.0f * std::sqrt(2.0f), 5.0f * std::sqrt(2.0f)) * Matrix::MakeRotate(45.0f));

  layerA1->addChild(layerA2);
  layerA2->addChild(layerA3);

  auto globalPoint = Point::Make(25.0f, 45.0f);
  auto pointInLayer3 = layerA3->globalToLocal(globalPoint);
  printf("pointInLayer3XY: (%f, %f)\n", pointInLayer3.x, pointInLayer3.y);
  EXPECT_EQ(pointInLayer3, Point::Make(15.0f, 5.0f));

  auto pointInLayer2 = layerA2->globalToLocal(globalPoint);
  printf("pointInLayer2XY: (%f, %f)\n", pointInLayer2.x, pointInLayer2.y);
  EXPECT_EQ(pointInLayer2, Point::Make(15.0f * std::sqrt(2.0f), 15.0f * std::sqrt(2.0f)));

  auto pointInLayer1 = layerA1->globalToLocal(globalPoint);
  printf("pointInLayer1XY: (%f, %f)\n", pointInLayer1.x, pointInLayer1.y);
  EXPECT_EQ(pointInLayer1, Point::Make(15.0f, 35.0f));
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
  auto layer1GlobalMat = layerA1->getTotalMatrix();
  printf("layer1GlobalMatXY: (%f, %f)\n", layer1GlobalMat.getTranslateX(), layer1GlobalMat.getTranslateY());

  auto layerA2 = Layer::Make();
  layerA2->setMatrix(Matrix::MakeTrans(10, 10) * Matrix::MakeRotate(45.0f));
  layerA1->addChild(layerA2);
  auto layer2GlobalMat = layerA2->getTotalMatrix();
  printf("layer2GlobalMatXY: (%f, %f)\n", layer2GlobalMat.getTranslateX(), layer2GlobalMat.getTranslateY());

  auto layerA3 = Layer::Make();
  layerA3->setMatrix(Matrix::MakeTrans(10 * std::sqrt(2.0f), 10 * std::sqrt(2.0f)) * Matrix::MakeRotate(45.0f));
  layerA2->addChild(layerA3);
  auto layer3GlobalMat = layerA3->getTotalMatrix();
  printf("layer3GlobalMatXY: (%f, %f)\n", layer3GlobalMat.getTranslateX(), layer3GlobalMat.getTranslateY());

  auto pointDInLayer3 = Point::Make(5, 5);
  auto pointDInGlobal = layerA3->localToGlobal(pointDInLayer3);
  printf("pointDInGlobalXY: (%f, %f)\n", pointDInGlobal.x, pointDInGlobal.y);
  EXPECT_EQ(pointDInGlobal, Point::Make(15.0f, 45.0f));

  auto pointEInLayer2 = Point::Make(8, 8);
  auto pointEInGlobal = layerA2->localToGlobal(pointEInLayer2);
  printf("pointEInGlobalXY: (%f, %f)\n", pointEInGlobal.x, pointEInGlobal.y);
  EXPECT_EQ(pointEInGlobal, Point::Make(layer2GlobalMat.getTranslateX(), layer2GlobalMat.getTranslateY() + 8.0f * std::sqrt(2.0f)));

  auto layer4 = Layer::Make();
  layer4->setMatrix(Matrix::MakeTrans(5, -5) * Matrix::MakeRotate(-60.0f));
  layerA3->addChild(layer4);
  auto layer4GlobalMat = layer4->getTotalMatrix();
  printf("layer4GlobalMatXY: (%f, %f)\n", layer4GlobalMat.getTranslateX(), layer4GlobalMat.getTranslateY());

  auto pointFInLayer4 = Point::Make(10.0f, 10.0f);
  auto pointFInGlobal = layer4->localToGlobal(pointFInLayer4);
  printf("pointFInGlobalXY: (%f, %f)\n", pointFInGlobal.x, pointFInGlobal.y);
  EXPECT_EQ(pointFInGlobal, Point::Make(28.660254f, 58.660255f));

  auto layer5 = Layer::Make();
  layer5->setMatrix(Matrix::MakeTrans(10, -15) * Matrix::MakeRotate(-90.0f));
  layerA3->addChild(layer5);
  auto layer5GlobalMat = layer5->getTotalMatrix();
  printf("layer5GlobalMatXY: (%f, %f)\n", layer5GlobalMat.getTranslateX(), layer5GlobalMat.getTranslateY());

  auto pointGInLayer5 = Point::Make(10.0f, 20.0f);
  auto pointGInGlobal = layer5->localToGlobal(pointGInLayer5);
  printf("pointGInGlobalXY: (%f, %f)\n", pointGInGlobal.x, pointGInGlobal.y);
  EXPECT_EQ(pointGInGlobal, Point::Make(45.0f, 70.0f));
}

}  // namespace tgfx
