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

#include <math.h>
#include <vector>
#include "core/filters/ComposeImageFilter.h"
#include "core/filters/GaussianBlurImageFilter.h"
#include "core/shaders/GradientShader.h"
#include "core/utils/MathExtra.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "layers/ContourContext.h"
#include "layers/DrawArgs.h"
#include "layers/RootLayer.h"
#include "layers/contents/RasterizedContent.h"
#include "tgfx/core/Shape.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/Gradient.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/ImagePattern.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/filters/BlendFilter.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/filters/ColorMatrixFilter.h"
#include "tgfx/layers/filters/DropShadowFilter.h"
#include "tgfx/layers/filters/InnerShadowFilter.h"
#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "tgfx/layers/layerstyles/InnerShadowStyle.h"
#include "utils/TestUtils.h"
#include "utils/common.h"

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
  EXPECT_EQ(child1->parent(), parent.get());
  EXPECT_EQ(child2->parent(), child1.get());
  EXPECT_TRUE(parent->children().size() == 1);
  EXPECT_TRUE(parent->contains(child1));
  EXPECT_TRUE(parent->contains(child2));
  EXPECT_EQ(parent->getChildIndex(child1), 0);
  EXPECT_EQ(parent->getChildIndex(child2), -1);
  parent->addChildAt(child3, 0);
  EXPECT_EQ(child3->parent(), parent.get());
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
  EXPECT_EQ(replacedChild->parent(), parent.get());
  EXPECT_EQ(replacedChild->root(), displayList->root());
  EXPECT_FALSE(parent->contains(child1));
  EXPECT_FALSE(parent->contains(child2));
  EXPECT_TRUE(parent->contains(replacedChild));
  EXPECT_TRUE(parent->children().size() == 2);
  EXPECT_EQ(parent->getChildIndex(replacedChild), 1);
  parent->replaceChild(replacedChild, child2);
  EXPECT_EQ(child2->parent(), parent.get());
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
  ContextScope scope;
  auto context = scope.getContext();
  auto surface = Surface::Make(context, 200, 100);
  auto displayList = std::make_unique<DisplayList>();
  auto layer = Layer::Make();
  displayList->root()->addChild(layer);
  auto textLayer = TextLayer::Make();
  textLayer->setName("text_layer1");
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
  textLayer2->setName("text_layer2");
  layer->addChild(textLayer2);
  textLayer2->setText("Hello, World!");
  textLayer2->setFont(font);
  textLayer2->setBlendMode(BlendMode::Difference);
  auto emojiTypeface = MakeTypeface("resources/font/NotoColorEmoji.ttf");
  tgfx::Font emojiFont(emojiTypeface, 10);
  auto emojiLayer = TextLayer::Make();
  layer->addChild(emojiLayer);
  emojiLayer->setText("🤡👻🐠🤩😃🤪");
  emojiLayer->setFont(emojiFont);
  emojiLayer->setMatrix(Matrix::MakeTrans(0, 20));
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/draw_text"));
}

TGFX_TEST(LayerTest, imageLayer) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/cmyk.jpg");
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
  EXPECT_EQ(greatGrandsonTotalMatrix, Matrix3D::MakeTranslate(30, 30, 0));

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

TGFX_TEST(LayerTest, getTightBounds) {
  auto root = Layer::Make();
  root->setMatrix(Matrix::MakeTrans(10, 10));

  auto rectShape = ShapeLayer::Make();
  auto rect = Rect::MakeXYWH(0.f, 0.f, 100.f, 100.f);
  Path path = {};
  path.addRect(rect);
  rectShape->setPath(path);
  rectShape->setFillStyle(SolidColor::Make(Color::Red()));
  auto strokeStyle = SolidColor::Make(Color::Black());
  rectShape->setStrokeStyle(strokeStyle);
  rectShape->setLineWidth(10);
  rectShape->setStrokeAlign(StrokeAlign::Outside);
  root->addChild(rectShape);
  auto bounds = rectShape->getBounds(root.get(), true);
  EXPECT_FLOAT_EQ(bounds.height(), 120);

  auto lineShape = ShapeLayer::Make();
  Path linePath = {};
  linePath.moveTo(0, 0);
  linePath.lineTo(100, 0);
  linePath.lineTo(50, 20);
  linePath.lineTo(20, -20);
  lineShape->setPath(linePath);
  lineShape->setStrokeStyle(SolidColor::Make(Color::Red()));
  auto matrix = Matrix::MakeRotate(50);
  matrix.postConcat(Matrix::MakeTrans(150, 20));
  lineShape->setMatrix(matrix);

  auto lineShapeChild = ShapeLayer::Make();
  Path linePath2 = {};
  linePath2.moveTo(0, 0);
  linePath2.lineTo(100, 0);
  linePath2.lineTo(50, 20);
  linePath2.lineTo(20, -20);
  lineShapeChild->setPath(linePath2);
  lineShapeChild->setStrokeStyle(SolidColor::Make(Color::Red()));
  matrix = Matrix::MakeTrans(100, 20);
  lineShapeChild->setMatrix(matrix);
  lineShape->addChild(lineShapeChild);

  root->addChild(lineShape);
  bounds = lineShape->getBounds(root.get(), true);
  EXPECT_FLOAT_EQ(bounds.width(), 114.002686f);
  EXPECT_FLOAT_EQ(bounds.height(), 166.826691f);

  auto lineBoundsShape = ShapeLayer::Make();
  Path lineBoundsRect = {};
  lineBoundsRect.addRect(bounds);
  lineBoundsShape->setPath(lineBoundsRect);
  lineBoundsShape->setStrokeStyle(SolidColor::Make(Color::Green()));
  root->addChild(lineBoundsShape);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 350, 250);
  auto displayList = std::make_unique<DisplayList>();
  displayList->root()->addChild(root);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/getTightBounds"));
}

TGFX_TEST(LayerTest, getbounds) {
  auto root = Layer::Make();
  root->setMatrix(Matrix::MakeTrans(10, 10));

  auto child = TextLayer::Make();
  child->setMatrix(Matrix::MakeRotate(20));
  child->setText("hello");
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  tgfx::Font font(typeface, 20);
  child->setFont(font);
  auto bounds = child->getBounds();
  EXPECT_FLOAT_EQ(bounds.left, -20.039999f);
  EXPECT_FLOAT_EQ(bounds.top, -7.2000007f);
  EXPECT_FLOAT_EQ(bounds.right, 93.5599975f);
  EXPECT_FLOAT_EQ(bounds.bottom, 49.9199982f);

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
  EXPECT_FLOAT_EQ(bounds.left, -20.039999f);
  EXPECT_FLOAT_EQ(bounds.top, -22.485762f);
  EXPECT_FLOAT_EQ(bounds.right, 94.183533f);
  EXPECT_FLOAT_EQ(bounds.bottom, 62.044159f);
  bounds = child->getBounds(root.get());
  EXPECT_FLOAT_EQ(bounds.left, -35.9050827f);
  EXPECT_FLOAT_EQ(bounds.top, -13.6198702f);
  EXPECT_FLOAT_EQ(bounds.right, 90.3801804f);
  EXPECT_FLOAT_EQ(bounds.bottom, 78.9088592f);
  bounds = child->getBounds(cousin.get());
  EXPECT_FLOAT_EQ(bounds.left, -45.9050827f);
  EXPECT_FLOAT_EQ(bounds.top, -23.6198692f);
  EXPECT_FLOAT_EQ(bounds.right, 80.3801804f);
  EXPECT_FLOAT_EQ(bounds.bottom, 68.9088592f);

  auto displayList = std::make_unique<DisplayList>();
  displayList->root()->addChild(root);
  bounds = child->getBounds();
  EXPECT_FLOAT_EQ(bounds.left, -20.039999f);
  EXPECT_FLOAT_EQ(bounds.top, -22.485762f);
  EXPECT_FLOAT_EQ(bounds.right, 94.183533f);
  EXPECT_FLOAT_EQ(bounds.bottom, 62.044159f);
  bounds = child->getBounds(root.get());
  EXPECT_FLOAT_EQ(bounds.left, -35.9050827f);
  EXPECT_FLOAT_EQ(bounds.top, -13.6198702f);
  EXPECT_FLOAT_EQ(bounds.right, 90.3801804f);
  EXPECT_FLOAT_EQ(bounds.bottom, 78.9088592f);
  bounds = child->getBounds(cousin.get());
  EXPECT_FLOAT_EQ(bounds.left, -45.9050827f);
  EXPECT_FLOAT_EQ(bounds.top, -23.6198692f);
  EXPECT_FLOAT_EQ(bounds.right, 80.3801804f);
  EXPECT_FLOAT_EQ(bounds.bottom, 68.9088592f);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto rootBounds = root->getBounds();
  auto width = static_cast<int>(rootBounds.width());
  auto height = static_cast<int>(rootBounds.height());
  auto surface = Surface::Make(context, width, height);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/getBounds"));
}

TGFX_TEST(LayerTest, shapeLayer) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 300);
  auto displayList = std::make_unique<DisplayList>();
  auto layer = Layer::Make();
  displayList->root()->addChild(layer);
  for (int i = 0; i < 3; i++) {
    auto shapeLayer = ShapeLayer::Make();
    auto rect = Rect::MakeXYWH(10, 10 + 100 * i, 140, 80);
    Path path = {};
    path.addRect(rect);
    shapeLayer->setPath(path);
    shapeLayer->removeFillStyles();
    std::shared_ptr<ShapeStyle> fillStyle = nullptr;
    switch (i) {
      case 0:
        fillStyle = Gradient::MakeLinear({rect.left, rect.top}, {rect.right, rect.top},
                                         {{0.f, 0.f, 1.f, 1.f}, {0.f, 1.f, 0.f, 1.f}});
        break;
      case 1:
        fillStyle = Gradient::MakeRadial({rect.centerX(), rect.centerY()}, rect.width() / 2.0f,
                                         {{0.f, 0.f, 1.f, 1.f}, {0.f, 1.f, 0.f, 1.f}});
        break;
      case 2:
        fillStyle = ImagePattern::Make(MakeImage("resources/apitest/imageReplacement.png"),
                                       TileMode::Repeat, TileMode::Mirror);
        std::static_pointer_cast<ImagePattern>(fillStyle)->setMatrix(
            Matrix::MakeTrans(-25, rect.top - 70));
        break;
      default:
        break;
    }
    fillStyle->setAlpha(0.8f);
    shapeLayer->addFillStyle(fillStyle);

    // stroke style
    shapeLayer->setLineWidth(10.0f);
    shapeLayer->setLineCap(LineCap::Butt);
    shapeLayer->setLineJoin(LineJoin::Miter);
    auto strokeStyle = SolidColor::Make(Color::Red());
    shapeLayer->setStrokeStyle(strokeStyle);
    strokeStyle = SolidColor::Make(Color::Green());
    strokeStyle->setAlpha(0.5f);
    strokeStyle->setBlendMode(BlendMode::Lighten);
    shapeLayer->addStrokeStyle(strokeStyle);
    if (i != 2) {
      std::vector<float> dashPattern = {10.0f, 10.0f};
      shapeLayer->setLineDashPattern(dashPattern);
      shapeLayer->setLineDashPhase(5.0f);
    }
    shapeLayer->setStrokeAlign(static_cast<StrokeAlign>(i));
    layer->addChild(shapeLayer);
    auto shapeLayerRect = shapeLayer->getBounds();
    switch (i) {
      case 0:
        EXPECT_EQ(shapeLayerRect, Rect::MakeLTRB(-10, -10, 170, 110));
        break;
      case 1:
        EXPECT_EQ(shapeLayerRect, Rect::MakeLTRB(10, 110, 150, 190));
        break;
      case 2:
        EXPECT_EQ(shapeLayerRect, Rect::MakeLTRB(-30, 170, 190, 330));
        break;
      default:
        break;
    }
  }
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/draw_shape"));
}

TGFX_TEST(LayerTest, solidLayer) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
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
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/draw_solid"));
}

TGFX_TEST(LayerTest, ZoomAndOffset) {
  auto image = MakeImage("resources/apitest/rotation.jpg");
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  auto displayList = std::make_unique<DisplayList>();
  auto layer = ImageLayer::Make();
  layer->setImage(image);
  auto matrix = Matrix::MakeScale(0.5f);
  matrix.postTranslate(200, 200);
  layer->setMatrix(matrix);
  displayList->root()->addChild(layer);
  displayList->setZoomScale(0.5f);
  displayList->setContentOffset(-300, -300);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/ZoomAndOffset"));
}

TGFX_TEST(LayerTest, StrokeOnTop) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto displayList = std::make_unique<DisplayList>();
  auto layer = Layer::Make();
  displayList->root()->addChild(layer);
  auto shapeLayer = ShapeLayer::Make();
  Path path = {};
  path.addRect(Rect::MakeXYWH(20, 20, 150, 150));
  shapeLayer->setPath(path);
  shapeLayer->setFillStyle(SolidColor::Make(Color::Red()));
  auto strokeColor = SolidColor::Make(Color::Green());
  strokeColor->setAlpha(0.5f);
  shapeLayer->setStrokeStyle(strokeColor);
  shapeLayer->setLineWidth(16);
  auto innerShadow = InnerShadowStyle::Make(30, 30, 0, 0, Color::FromRGBA(100, 0, 0, 128));
  auto dropShadow = DropShadowStyle::Make(-20, -20, 0, 0, Color::Black());
  dropShadow->setShowBehindLayer(false);
  shapeLayer->setLayerStyles({dropShadow, innerShadow});
  shapeLayer->setExcludeChildEffectsInLayerStyle(true);
  layer->addChild(shapeLayer);
  auto solidLayer = SolidLayer::Make();
  solidLayer->setWidth(100);
  solidLayer->setHeight(100);
  solidLayer->setRadiusX(25);
  solidLayer->setRadiusY(25);
  solidLayer->setMatrix(Matrix::MakeTrans(75, 75));
  solidLayer->setColor(Color::Blue());
  shapeLayer->addChild(solidLayer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/StrokeOnTop_Off"));
  shapeLayer->setStrokeOnTop(true);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/StrokeOnTop_On"));
}

TGFX_TEST(LayerTest, FilterTest) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/filterTest"));
}

TGFX_TEST(LayerTest, filterClip) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/filterClip"));
}

TGFX_TEST(LayerTest, dropshadowLayerFilter) {
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

  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/dropShadow"));

  auto src = Rect::MakeXYWH(10, 10, 10, 10);
  auto bounds = filter4->getImageFilter(1.0f)->filterBounds(src);
  EXPECT_EQ(bounds, Rect::MakeXYWH(10, 10, 13, 13));
  bounds = ImageFilter::DropShadowOnly(3, 3, 0, 0, Color::White())->filterBounds(src);
  EXPECT_EQ(bounds, Rect::MakeXYWH(13, 13, 10, 10));
}

TGFX_TEST(LayerTest, colorBlendLayerFilter) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/ModeColorFilter"));
}

TGFX_TEST(LayerTest, colorMatrixLayerFilter) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/identityMatrix"));
  std::array<float, 20> greyColorMatrix = {0.21f, 0.72f, 0.07f, 0.41f, 0,  // red
                                           0.21f, 0.72f, 0.07f, 0.41f, 0,  // green
                                           0.21f, 0.72f, 0.07f, 0.41f, 0,  // blue
                                           0,     0,     0,     1.0f,  0};

  filter->setMatrix(greyColorMatrix);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/greyColorMatrix"));
}

TGFX_TEST(LayerTest, blurLayerFilter) {
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

TGFX_TEST(LayerTest, PassthroughAndNormal) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 800, 400);

  surface->getCanvas()->clear(Color::FromRGBA(53, 53, 53));
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
  displayList.setRenderMode(RenderMode::Direct);
  displayList.render(surface.get(), false);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PassThoughAndNormal"));
}

TGFX_TEST(LayerTest, imageMask) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/imageMask"));
}

TGFX_TEST(LayerTest, shapeMask) {
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
  auto radialFilleStyle =
      Gradient::MakeRadial({500, 500}, 500, {{1.f, 0.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f}});
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
  auto filleStyle = SolidColor::Make(Color::Red());
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
  auto vectorFillStyle = SolidColor::Make(Color::FromRGBA(0, 0, 255, 128));
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
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/shapeMask"));
}

TGFX_TEST(LayerTest, textMask) {
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
  EXPECT_EQ(alphaLayerBounds, Rect::MakeLTRB(1761, 746, 3024, 1392));

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
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/textMask"));
}

TGFX_TEST(LayerTest, HasContentChanged) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 150, 150);
  DisplayList displayList;
  auto shapeLayer = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeXYWH(0, 0, 100, 100));
  shapeLayer->setPath(path);
  shapeLayer->setFillStyle(SolidColor::Make(Color::FromRGBA(255, 0, 0)));
  displayList.root()->addChild(shapeLayer);
  EXPECT_TRUE(displayList.hasContentChanged());
  displayList.render(surface.get());
  context->flushAndSubmit();
  EXPECT_FALSE(displayList.hasContentChanged());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/HasContentChanged_Org"));
  displayList.setContentOffset(50, 50);
  EXPECT_TRUE(displayList.hasContentChanged());
  displayList.render(surface.get(), false);
  context->flushAndSubmit();
  EXPECT_FALSE(displayList.hasContentChanged());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/HasContentChanged_Offset"));
  displayList.setZoomScale(0.5f);
  EXPECT_TRUE(displayList.hasContentChanged());
  displayList.render(surface.get());
  context->flushAndSubmit();
  EXPECT_FALSE(displayList.hasContentChanged());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/HasContentChanged_Zoom"));
}

/**
 * The schematic diagram is as follows:
 * https://www.geogebra.org/graphing/uxs8drhd
 */
TGFX_TEST(LayerTest, getLayersUnderPoint) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
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
  auto imageLayerBounds = imageLayer->getBounds(nullptr, true);
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
  auto shaperLayerBounds = shaperLayer->getBounds(nullptr, true);
  shaperLayer->getGlobalMatrix().mapRect(&shaperLayerBounds);
  printf("shaperLayerBounds: (%f, %f, %f, %f)\n", shaperLayerBounds.left, shaperLayerBounds.top,
         shaperLayerBounds.right, shaperLayerBounds.bottom);

  auto textLayer = TextLayer::Make();
  textLayer->setName("text_layer");
  textLayer->setText("Hello World!");
  textLayer->setMatrix(Matrix::MakeTrans(50.0f, 0.0f) * Matrix::MakeScale(5.0f, 5.0f));
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  tgfx::Font font(typeface, 20);
  textLayer->setFont(font);
  rootLayer->addChild(textLayer);
  auto textLayerBounds = textLayer->getBounds(nullptr, true);
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
  auto shaperLayer2Bounds = shaperLayer2->getBounds(nullptr, true);
  shaperLayer2->getGlobalMatrix().mapRect(&shaperLayer2Bounds);
  printf("shaperLayer2Bounds: (%f, %f, %f, %f)\n", shaperLayer2Bounds.left, shaperLayer2Bounds.top,
         shaperLayer2Bounds.right, shaperLayer2Bounds.bottom);

  auto shapeLayerInvisibleFill = ShapeLayer::Make();
  auto rectPath1 = tgfx::Path();
  rectPath1.addRect({100, 300, 200, 375});
  shapeLayerInvisibleFill->setPath(rectPath1);
  shapeLayerInvisibleFill->setName("shapeLayerInvisibleFill");
  auto fillStyle0 = SolidColor::Make(Color::FromRGBA(130, 182, 41, 0));
  shapeLayerInvisibleFill->setFillStyle(fillStyle0);
  rootLayer->addChild(shapeLayerInvisibleFill);
  auto shapeLayerInvisibleFillBounds = shapeLayerInvisibleFill->getBounds(nullptr, true);
  shapeLayerInvisibleFill->getGlobalMatrix().mapRect(&shapeLayerInvisibleFillBounds);
  printf("shapeLayerInvisibleFillBounds: (%f, %f, %f, %f)\n", shapeLayerInvisibleFillBounds.left,
         shapeLayerInvisibleFillBounds.top, shapeLayerInvisibleFillBounds.right,
         shapeLayerInvisibleFillBounds.bottom);

  auto shapeLayerInvisibleStroke = ShapeLayer::Make();
  auto rectPath2 = tgfx::Path();
  rectPath2.addRect({150, 320, 250, 395});
  shapeLayerInvisibleStroke->setPath(rectPath2);
  shapeLayerInvisibleStroke->setName("shapeLayerInvisibleStroke");
  shapeLayerInvisibleStroke->addStrokeStyle(
      tgfx::SolidColor::Make(tgfx::Color::FromRGBA(130, 182, 41, 0)));
  rootLayer->addChild(shapeLayerInvisibleStroke);

  auto shapeLayerInvisibleStrokeBounds = shapeLayerInvisibleStroke->getBounds(nullptr, true);
  shapeLayerInvisibleStroke->getGlobalMatrix().mapRect(&shapeLayerInvisibleStrokeBounds);
  printf("shapeLayerInvisibleStrokeBounds: (%f, %f, %f, %f)\n",
         shapeLayerInvisibleStrokeBounds.left, shapeLayerInvisibleStrokeBounds.top,
         shapeLayerInvisibleStrokeBounds.right, shapeLayerInvisibleStrokeBounds.bottom);

  auto rootLayerBounds = rootLayer->getBounds(nullptr, true);
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
  canvas->drawRect(shapeLayerInvisibleFillBounds, paint);
  canvas->drawRect(shapeLayerInvisibleStrokeBounds, paint);
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
  EXPECT_EQ(static_cast<int>(layers.size()), 3);
  EXPECT_EQ(layerNameJoin, "text_layer|shaper_layer|root_layer|");

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
  EXPECT_EQ(static_cast<int>(layers.size()), 2);
  EXPECT_EQ(layerNameJoin, "text_layer|root_layer|");

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
  EXPECT_EQ(static_cast<int>(layers.size()), 3);
  EXPECT_EQ(layerNameJoin, "shaper_layer2|text_layer|root_layer|");

  // P12(180, 360) is in the shaper_layer2, root_layer
  layerNameJoin = "";
  layers = rootLayer->getLayersUnderPoint(180.0f, 360.0f);
  canvas->drawCircle(180.0f, 360.0f, 5.0f, paint);
  printf("layers.size(): %zu\n", layers.size());
  for (auto layer : layers) {
    printf("layer: %s\n", layer->name().c_str());
    layerNameJoin += layer->name() + "|";
  }
  printf("\n");
  EXPECT_EQ(static_cast<int>(layers.size()), 3);
  EXPECT_EQ(layerNameJoin, "shapeLayerInvisibleStroke|shapeLayerInvisibleFill|root_layer|");
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/getLayersUnderPoint"));
}

/**
 * The schematic diagram is as follows(Visit geogebra online vector map to view pixel details):
 * https://www.geogebra.org/classic/krbzbz6m
 */
TGFX_TEST(LayerTest, hitTestPoint) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
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
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/Layer_hitTestPoint"));
}

TGFX_TEST(LayerTest, drawRRect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1000, 600);
  auto canvas = surface->getCanvas();

  auto fillPaint = Paint();
  fillPaint.setStyle(PaintStyle::Fill);
  fillPaint.setColor(Color::Red());
  Rect rect = Rect::MakeXYWH(50, 50, 200, 160);
  RRect rRect = {};
  rRect.setRectXY(rect, 10.0f, 10.0f);
  canvas->drawRRect(rRect, fillPaint);

  auto strokePaint = Paint();
  strokePaint.setStyle(PaintStyle::Stroke);
  strokePaint.setStrokeWidth(10.0f);
  strokePaint.setColor(Color::Red());

  Rect rect1 = Rect::MakeXYWH(300, 50, 200, 160);
  RRect rRect1 = {};
  rRect1.setRectXY(rect1, 10.0f, 10.0f);
  canvas->drawRRect(rRect1, strokePaint);

  Rect rect2 = Rect::MakeXYWH(600, 50, 200, 160);
  RRect rRect2 = {};
  rRect2.setRectXY(rect2, 15.0f, 10.0f);
  canvas->drawRRect(rRect2, strokePaint);

  Rect rect3 = Rect::MakeXYWH(50, 300, 200, 160);
  RRect rRect3 = {};
  rRect3.setRectXY(rect3, 100.0f, 150.0f);
  canvas->drawRRect(rRect3, strokePaint);

  Rect rect4 = Rect::MakeXYWH(300, 300, 200, 160);
  RRect rRect4 = {};
  rRect4.setRectXY(rect4, 50.0f, 10.0f);
  canvas->drawRRect(rRect4, strokePaint);

  auto strokePaint2 = Paint();
  strokePaint2.setStyle(PaintStyle::Stroke);
  strokePaint2.setStrokeWidth(50.0f);
  strokePaint2.setColor(Color::Red());

  Rect rect5 = Rect::MakeXYWH(600, 300, 200, 160);
  RRect rRect5 = {};
  rRect5.setRectXY(rect5, 20.f, 10.f);
  canvas->drawRRect(rRect5, strokePaint2);

  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/Layer_drawRRect"));
}

/**
 * The schematic diagram is as follows:
 * https://www.geogebra.org/classic/nxwbmmrp
 */
TGFX_TEST(LayerTest, hitTestPointNested) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
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
  auto imageLayerBounds = imageLayer->getBounds(nullptr, true);
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
  auto shaperLayerBounds = shaperLayer->getBounds(nullptr, true);
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
  auto textLayerBounds = textLayer->getBounds(nullptr, true);
  textLayer->getGlobalMatrix().mapRect(&textLayerBounds);
  printf("textLayerBounds: (%f, %f, %f, %f)\n", textLayerBounds.left, textLayerBounds.top,
         textLayerBounds.right, textLayerBounds.bottom);

  auto rootLayerBounds = rootLayer->getBounds(nullptr, true);
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

  // P0(320.0, 340.0)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point p0 = {320.0f, 340.0f};
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

  // P1(280.0, 360.0)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point p1 = {280.0f, 360.0f};
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

  // P2(150.0, 170.0)
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  Point p2 = {150.0f, 170.0f};
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
  EXPECT_EQ(true, textLayer->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(false, textLayer->hitTestPoint(p3.x, p3.y, true));
  EXPECT_EQ(false, shaperLayer->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(false, shaperLayer->hitTestPoint(p3.x, p3.y, true));
  EXPECT_EQ(true, imageLayer->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(true, imageLayer->hitTestPoint(p3.x, p3.y, true));
  EXPECT_EQ(true, parentLayer->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(true, parentLayer->hitTestPoint(p3.x, p3.y, true));
  EXPECT_EQ(true, childLayer->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(false, childLayer->hitTestPoint(p3.x, p3.y, true));
  EXPECT_EQ(true, grandsonLayer->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(false, grandsonLayer->hitTestPoint(p3.x, p3.y, true));
  EXPECT_EQ(true, rootLayer->hitTestPoint(p3.x, p3.y));
  EXPECT_EQ(true, rootLayer->hitTestPoint(p3.x, p3.y, true));
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/Layer_hitTestPointNested"));
}

TGFX_TEST(LayerTest, InnerShadowFilter) {
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

  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/innerShadow"));
}

TGFX_TEST(LayerTest, DirtyFlag) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto displayList = std::make_unique<DisplayList>();
  auto surface = Surface::Make(context, 100, 100);
  auto child = Layer::Make();
  displayList->root()->addChild(child);

  auto grandChild = ImageLayer::Make();
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  EXPECT_TRUE(image != nullptr);
  grandChild->setImage(image);
  grandChild->setMatrix(Matrix::MakeTrans(10, 10));
  grandChild->setVisible(false);
  child->addChild(grandChild);

  auto child2 = ImageLayer::Make();
  child2->setImage(image);
  displayList->root()->addChild(child2);

  displayList->render(surface.get());

  auto root = displayList->root();
  EXPECT_TRUE(grandChild->bitFields.dirtyDescendents);
  EXPECT_TRUE(grandChild->layerContent == nullptr && grandChild->bitFields.dirtyContent);
  EXPECT_TRUE(!child->bitFields.dirtyDescendents && !child->bitFields.dirtyContent);
  EXPECT_TRUE(!root->bitFields.dirtyDescendents && !root->bitFields.dirtyContent);

  grandChild->setVisible(true);
  EXPECT_TRUE(grandChild->bitFields.dirtyDescendents);
  EXPECT_TRUE(grandChild->layerContent == nullptr && grandChild->bitFields.dirtyContent);
  EXPECT_TRUE(child->bitFields.dirtyDescendents);
  EXPECT_TRUE(root->bitFields.dirtyDescendents);
  displayList->render(surface.get());

  EXPECT_TRUE(!grandChild->bitFields.dirtyDescendents && !grandChild->bitFields.dirtyContent);
  EXPECT_TRUE(grandChild->layerContent != nullptr);
  EXPECT_TRUE(!child->bitFields.dirtyDescendents && !child->bitFields.dirtyContent);
  EXPECT_TRUE(!root->bitFields.dirtyDescendents && !root->bitFields.dirtyContent);

  child->setVisible(false);
  EXPECT_TRUE(!grandChild->bitFields.dirtyDescendents && !grandChild->bitFields.dirtyContent);
  EXPECT_TRUE(grandChild->layerContent != nullptr);
  EXPECT_TRUE(!child->bitFields.dirtyDescendents && !child->bitFields.dirtyContent);
  EXPECT_TRUE(root->bitFields.dirtyDescendents && !root->bitFields.dirtyContent);
}

TGFX_TEST(LayerTest, DropShadowStyle) {
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
  auto fillStyle = SolidColor::Make(Color::FromRGBA(100, 0, 0, 128));
  layer->setFillStyle(fillStyle);
  layer->setLineWidth(2.0f);
  layer->setBlendMode(BlendMode::Lighten);

  auto shadowLayer = Layer::Make();
  auto style = DropShadowStyle::Make(10, 10, 0, 0, Color::Black(), false);
  shadowLayer->setLayerStyles({style});
  shadowLayer->addChild(layer);
  shadowLayer->setExcludeChildEffectsInLayerStyle(true);
  back->addChild(shadowLayer);
  displayList->root()->addChild(back);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DropShadowStyle"));

  style->setBlendMode(BlendMode::Luminosity);
  style->setOffsetX(0);
  style->setOffsetY(-20);
  style->setShowBehindLayer(true);
  shadowLayer->setAlpha(0.5);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DropShadowStyle2"));

  layer->setBlendMode(BlendMode::Multiply);
  layer->setFillStyle(nullptr);
  layer->setStrokeStyle(SolidColor::Make(Color::FromRGBA(100, 0, 0, 128)));
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DropShadowStyle-stroke-behindLayer"));

  style->setShowBehindLayer(false);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DropShadowStyle-stroke"));

  auto blur = BlurFilter::Make(2.5, 2.5);
  layer->setFilters({blur});
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DropShadowStyle-stroke-blur"));

  style->setShowBehindLayer(true);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DropShadowStyle-stroke-blur-behindLayer"));
}

TGFX_TEST(LayerTest, InnerShadowStyle) {
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
  auto fillStyle = SolidColor::Make(Color::FromRGBA(100, 0, 0, 128));
  layer->setFillStyle(fillStyle);
  auto style = InnerShadowStyle::Make(10, 10, 0, 0, Color::Black());
  layer->setLayerStyles({style});
  displayList->root()->addChild(layer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/InnerShadowStyle"));
}

TGFX_TEST(LayerTest, Filters) {
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
  auto fillStyle = SolidColor::Make(Color::FromRGBA(100, 0, 0, 128));
  layer->setFillStyle(fillStyle);
  auto filter = BlurFilter::Make(5, 5);
  auto filter2 = DropShadowFilter::Make(10, 10, 0, 0, Color::Black());
  auto filter3 = InnerShadowFilter::Make(10, 10, 0, 0, Color::White());
  layer->setFilters({filter, filter2, filter3});
  displayList->root()->addChild(layer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/filters"));
}

TGFX_TEST(LayerTest, MaskOnwer) {
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
  mask->setFillStyle(SolidColor::Make());

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

TGFX_TEST(LayerTest, BackgroundBlur) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 150, 150);
  auto canvas = surface->getCanvas();
  canvas->clipRect(Rect::MakeWH(150, 150));
  auto displayList = std::make_unique<DisplayList>();
  auto solidLayer = SolidLayer::Make();
  solidLayer->setColor(Color::Blue());
  solidLayer->setWidth(150);
  solidLayer->setHeight(150);
  displayList->root()->addChild(solidLayer);

  auto background = ImageLayer::Make();
  background->setImage(MakeImage("resources/apitest/imageReplacement.png"));
  displayList->root()->addChild(background);

  auto layer = ShapeLayer::Make();
  layer->setMatrix(Matrix::MakeTrans(30, 30));
  Path path;
  path.addRect(Rect::MakeWH(100, 100));
  layer->setPath(path);
  auto strokeStyle = SolidColor::Make(Color::FromRGBA(100, 0, 0, 100));
  layer->setStrokeStyle(strokeStyle);
  layer->setLineWidth(10);
  layer->setStrokeOnTop(true);
  layer->setExcludeChildEffectsInLayerStyle(true);
  auto filter = BackgroundBlurStyle::Make(2, 2);
  auto dropShadow = DropShadowStyle::Make(10, 10, 0, 0, Color::FromRGBA(0, 0, 0, 100));
  dropShadow->setShowBehindLayer(true);
  layer->setExcludeChildEffectsInLayerStyle(true);
  layer->setLayerStyles({dropShadow, filter});

  auto blurFilter = BlurFilter::Make(1, 2);
  layer->setFilters({blurFilter});

  auto silbing = ShapeLayer::Make();
  Path rect;
  rect.addRect(Rect::MakeWH(50, 50));
  silbing->setPath(rect);
  silbing->setMatrix(Matrix::MakeTrans(-10, 0));
  auto newBackgroundBlur = BackgroundBlurStyle::Make(3, 3);
  silbing->setLayerStyles({dropShadow, newBackgroundBlur});
  silbing->setFillStyle(SolidColor::Make(Color::FromRGBA(0, 0, 100, 100)));
  layer->addChild(silbing);

  auto clipLayer = Layer::Make();
  clipLayer->setMatrix(Matrix::MakeTrans(2, 40));
  clipLayer->setScrollRect(Rect::MakeXYWH(10, 10, 20, 20));
  layer->addChild(clipLayer);

  auto child = ShapeLayer::Make();

  child->setPath(rect);
  child->setMatrix(Matrix::MakeScale(0.5, 0.5));
  auto fillStyle2 = SolidColor::Make(Color::FromRGBA(0, 100, 0, 100));
  child->setFillStyle(fillStyle2);
  auto backgroundBlur = BackgroundBlurStyle::Make(5, 5);
  child->setLayerStyles({backgroundBlur});
  child->setBlendMode(BlendMode::Multiply);
  clipLayer->addChild(child);

  displayList->root()->addChild(layer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/backgroundLayerBlur"));
}

TGFX_TEST(LayerTest, MaskAlpha) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  Path path;
  path.addRect(Rect::MakeWH(100, 100));

  auto layer = ShapeLayer::Make();
  layer->setPath(path);
  auto layer_style = tgfx::SolidColor::Make({0.0f, 1.0f, 0.0f, 1.0f});
  layer->setFillStyle(layer_style);

  auto mask = ShapeLayer::Make();
  mask->setPath(path);
  mask->setMatrix(Matrix::MakeTrans(50, 50));
  auto mask_style = tgfx::SolidColor::Make({1.0f, 0.0f, 0.0f, 1.0f});
  mask_style->setAlpha(0);
  mask->setFillStyle(mask_style);

  layer->setMask(mask);

  list.root()->addChild(layer);
  list.root()->addChild(mask);
  auto surface = Surface::Make(context, 150, 150);
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/MaskAlpha"));
}

TGFX_TEST(LayerTest, ChildMask) {
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
  auto layer_style = SolidColor::Make(Color::Red());
  layer->setFillStyle(layer_style);

  auto layer2 = ShapeLayer::Make();
  layer2->setPath(path);
  auto layer2_matrix = Matrix::MakeTrans(100, 0);
  layer2_matrix.postConcat(init_trans);
  layer2->setMatrix(layer2_matrix);
  auto layer2_style = SolidColor::Make(Color::Green());
  layer2->setFillStyle(layer2_style);

  auto mask = ShapeLayer::Make();
  mask->setPath(path);
  auto mask_matrix = Matrix::MakeTrans(50, 50);
  mask_matrix.postConcat(init_trans);
  mask->setMatrix(mask_matrix);
  auto mask_style = SolidColor::Make(Color::Blue());
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
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/ChildMask"));
}

TGFX_TEST(LayerTest, InvalidMask) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  Path path;
  path.addRect(Rect::MakeWH(10, 10));
  auto shapeLayer = ShapeLayer::Make();
  shapeLayer->setPath(path);
  auto fillStyle = SolidColor::Make(Color::Red());
  shapeLayer->setFillStyle(fillStyle);

  auto maskLayer = ShapeLayer::Make();
  maskLayer->setPath(path);
  auto maskFillStyle = SolidColor::Make(Color::FromRGBA(0, 0, 0, 128));
  maskLayer->setFillStyle(maskFillStyle);
  maskLayer->setVisible(false);

  shapeLayer->setMask(maskLayer);

  list.root()->addChild(shapeLayer);
  list.root()->addChild(maskLayer);

  auto surface = Surface::Make(context, 10, 10);

  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/InvalidMask"));
}

TGFX_TEST(LayerTest, LargeScale) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  auto shapeLayer = ShapeLayer::Make();
  Path path = {};
  path.addRect(Rect::MakeWH(10000, 10000));
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  auto imagePattern = ImagePattern::Make(image);
  imagePattern->setMatrix(Matrix::MakeTrans(-20, -20));
  shapeLayer->setFillStyle(imagePattern);
  shapeLayer->setPath(path);
  list.root()->addChild(shapeLayer);

  auto surface = Surface::Make(context, 1000, 1000);

  shapeLayer->setMatrix(Matrix::MakeScale(256, 256));
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LargeScale"));
}

TGFX_TEST(LayerTest, ShapeStyleWithMatrix) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  Path path;
  path.addRect(Rect::MakeWH(100, 100));

  auto layer = ShapeLayer::Make();
  layer->setPath(path);
  auto matrix = Matrix::MakeScale(0.5f, 1.f);
  auto layerStyle =
      Gradient::MakeDiamond(Point::Make(100, 50), 50, {Color::Red(), Color::Blue()}, {0, 1});
  layerStyle->setMatrix(matrix);
  layer->setFillStyle(layerStyle);
  list.root()->addChild(layer);

  auto layer2 = ShapeLayer::Make();
  layer2->setPath(path);
  auto imageStyle = ImagePattern::Make(MakeImage("resources/apitest/imageReplacement.png"),
                                       TileMode::Decal, TileMode::Decal);
  imageStyle->setMatrix(matrix);
  layer2->setFillStyle(imageStyle);
  layer2->setMatrix(Matrix::MakeTrans(100, 0));
  list.root()->addChild(layer2);

  auto layer3 = ShapeLayer::Make();
  layer3->setPath(path);
  auto solidStyle = SolidColor::Make(Color::Red());
  solidStyle->setMatrix(matrix);
  layer3->setFillStyle(solidStyle);
  layer3->setMatrix(Matrix::MakeTrans(200, 0));
  list.root()->addChild(layer3);

  auto surface = Surface::Make(context, 300, 100);
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/ShapeStyleWithMatrix"));
}

TGFX_TEST(LayerTest, RasterizedCache) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 350, 350);
  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();
  rootLayer->setMatrix(Matrix::MakeTrans(30, 30));

  auto imageLayer = ImageLayer::Make();
  imageLayer->setImage(MakeImage("resources/apitest/imageReplacement.png"));
  imageLayer->setShouldRasterize(true);
  rootLayer->addChild(imageLayer);

  auto rectLayer = ShapeLayer::Make();
  auto style = DropShadowStyle::Make(10, 10, 0, 0, Color::Black(), false);
  Path rect;
  rect.addRect(Rect::MakeWH(50, 50));
  rectLayer->setPath(rect);
  rectLayer->setFillStyle(SolidColor::Make(Color::Red()));
  rectLayer->setShouldRasterize(true);
  rectLayer->setLayerStyles({style});
  rectLayer->setMatrix(Matrix::MakeTrans(150, 0));
  imageLayer->addChild(rectLayer);

  auto blurLayer = ShapeLayer::Make();
  Path childPath;
  childPath.addRect(Rect::MakeWH(100, 100));
  blurLayer->setPath(childPath);
  auto fillStyle = SolidColor::Make(Color::FromRGBA(100, 0, 0, 128));
  blurLayer->setFillStyle(fillStyle);
  blurLayer->setShouldRasterize(true);
  blurLayer->setMatrix(Matrix::MakeTrans(150, 0));
  blurLayer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  imageLayer->addChild(blurLayer);

  displayList->root()->addChild(rootLayer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/RasterizedCache"));
}

TGFX_TEST(LayerTest, RasterizedBackground) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 150, 150);
  auto displayList = std::make_unique<DisplayList>();
  auto solidLayer = SolidLayer::Make();
  solidLayer->setColor(Color::Blue());
  solidLayer->setWidth(150);
  solidLayer->setHeight(150);
  displayList->root()->addChild(solidLayer);

  auto background = ImageLayer::Make();
  background->setImage(MakeImage("resources/apitest/imageReplacement.png"));
  displayList->root()->addChild(background);

  auto parent = Layer::Make();
  parent->setMatrix(Matrix::MakeTrans(30, 30));
  parent->addChild(background);

  auto layerBeforeChild = ShapeLayer::Make();
  auto path = Path();
  path.addRect(Rect::MakeWH(50, 50));
  layerBeforeChild->setPath(path);
  layerBeforeChild->setFillStyle(SolidColor::Make(Color::Red()));

  auto backgroundNephew = ShapeLayer::Make();
  backgroundNephew->setPath(path);
  backgroundNephew->setMatrix(Matrix::MakeTrans(10, 10));
  backgroundNephew->setFillStyle(SolidColor::Make(Color::Green()));

  auto child = ShapeLayer::Make();
  Path childPath;
  childPath.addRect(Rect::MakeWH(100, 100));
  child->setPath(childPath);
  auto fillStyle = SolidColor::Make(Color::FromRGBA(100, 0, 0, 128));
  child->setFillStyle(fillStyle);
  child->setShouldRasterize(true);
  child->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  parent->addChild(child);
  displayList->root()->addChild(parent);

  displayList->render(surface.get());
  background->setMatrix(Matrix::MakeTrans(50, 50));
  auto rasterizedContent =
      static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage();
  displayList->render(surface.get());
  EXPECT_TRUE(rasterizedContent !=
              static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage());

  child->setMatrix(Matrix::MakeTrans(20, 20));
  rasterizedContent = static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage();
  displayList->render(surface.get());
  EXPECT_TRUE(rasterizedContent !=
              static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage());

  auto layerNextChild = ShapeLayer::Make();
  layerNextChild->setPath(path);
  layerNextChild->setMatrix(Matrix::MakeTrans(10, 10));
  layerNextChild->setFillStyle(SolidColor::Make(Color::FromRGBA(0, 100, 0, 128)));
  parent->addChild(layerNextChild);
  rasterizedContent = static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage();
  displayList->render(surface.get());
  EXPECT_TRUE(rasterizedContent ==
              static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage());

  auto grandChild = ShapeLayer::Make();
  grandChild->setPath(path);
  grandChild->setMatrix(Matrix::MakeTrans(10, 10));
  grandChild->setFillStyle(SolidColor::Make(Color::FromRGBA(0, 0, 100, 128)));
  child->addChild(grandChild);
  EXPECT_TRUE(child->rasterizedContent == nullptr);
  displayList->render(surface.get());

  auto nephew = ShapeLayer::Make();
  nephew->setPath(path);
  nephew->setMatrix(Matrix::MakeTrans(10, 10));
  nephew->setFillStyle(SolidColor::Make(Color::FromRGBA(0, 100, 0, 128)));
  layerNextChild->addChild(nephew);
  rasterizedContent = static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage();
  displayList->render(surface.get());
  EXPECT_TRUE(rasterizedContent ==
              static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage());

  parent->addChildAt(layerBeforeChild, parent->getChildIndex(child));
  rasterizedContent = static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage();
  displayList->render(surface.get());
  EXPECT_TRUE(rasterizedContent !=
              static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage());

  layerBeforeChild->addChildAt(backgroundNephew, 0);
  rasterizedContent = static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage();
  displayList->render(surface.get());
  EXPECT_TRUE(rasterizedContent !=
              static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage());

  layerBeforeChild->removeChildren();
  rasterizedContent = static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage();
  displayList->render(surface.get());
  EXPECT_TRUE(rasterizedContent !=
              static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage());

  layerBeforeChild->removeFromParent();
  rasterizedContent = static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage();
  displayList->render(surface.get());
  EXPECT_TRUE(rasterizedContent !=
              static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage());

  parent->setChildIndex(background, static_cast<int>(parent->children().size() - 1u));
  rasterizedContent = static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage();
  displayList->render(surface.get());
  EXPECT_TRUE(rasterizedContent !=
              static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage());

  parent->setChildIndex(background, 0);
  rasterizedContent = static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage();
  displayList->render(surface.get());
  EXPECT_TRUE(rasterizedContent !=
              static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage());

  parent->replaceChild(background, layerBeforeChild);
  rasterizedContent = static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage();
  displayList->render(surface.get());
  EXPECT_TRUE(rasterizedContent !=
              static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage());

  parent->replaceChild(layerNextChild, background);
  rasterizedContent = static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage();
  displayList->render(surface.get());
  // Ideally, rasterizedContent should remain unchanged here, but we need to call root->invalidateRect()
  // whenever a layer is removed or its index changes. As a result, dirty rects are always treated
  // as background changes. This is a trade-off between performance and correctness.
  EXPECT_TRUE(rasterizedContent !=
              static_cast<RasterizedContent*>(child->rasterizedContent.get())->getImage());
}

TGFX_TEST(LayerTest, AdaptiveDashEffect) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 400);
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->drawColor(Color::White());
  Path path = {};
  path.addRect(50, 50, 250, 150);
  path.addOval(Rect::MakeXYWH(50, 200, 200, 50));
  path.moveTo(50, 300);
  path.cubicTo(100, 300, 100, 350, 150, 350);
  path.quadTo(200, 350, 200, 300);
  std::vector<float> dashList = {10.f, 10.f};
  auto shapeLayer = ShapeLayer::Make();
  shapeLayer->setPath(path);
  auto strokeStyle = SolidColor::Make(Color::FromRGBA(100, 0, 0));
  shapeLayer->setLineWidth(1);
  shapeLayer->setStrokeStyle(strokeStyle);
  shapeLayer->setLineDashAdaptive(true);
  shapeLayer->setLineDashPattern(dashList);
  shapeLayer->setLineDashPhase(5);
  DisplayList displayList;
  displayList.root()->addChild(shapeLayer);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/AdaptiveDashEffect"));
}

TGFX_TEST(LayerTest, BottomLeftSurface) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto proxy = RenderTargetProxy::Make(context, 200, 200, false, 1, false, ImageOrigin::BottomLeft);
  auto surface = Surface::MakeFrom(std::move(proxy), 0, true);

  // parent
  auto parentFrame = tgfx::Rect::MakeXYWH(60, 110, 40, 40);

  auto childFrame = tgfx::Rect::MakeWH(150, 150);
  auto childLayer = tgfx::ShapeLayer::Make();
  childLayer->setExcludeChildEffectsInLayerStyle(true);

  tgfx::Path childPath;
  childPath.addRect(childFrame);
  childLayer->setPath(childPath);
  childLayer->setFillStyles({tgfx::SolidColor::Make(tgfx::Color::Red())});

  // contents
  auto contentsLayer = tgfx::Layer::Make();
  contentsLayer->setMatrix(tgfx::Matrix::MakeRotate(3));
  contentsLayer->setScrollRect(parentFrame);
  auto childMatrix = tgfx::Matrix::MakeTrans(50, 100);
  childMatrix.postRotate(3);
  contentsLayer->setMatrix(childMatrix);
  contentsLayer->addChild(childLayer);
  DisplayList displayList;

  displayList.root()->addChild(contentsLayer);
  displayList.render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/BottomLeftSurface"));
}

TGFX_TEST(LayerTest, DirtyRegionTest) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1024, 800);
  auto displayList = std::make_unique<DisplayList>();
  displayList->showDirtyRegions(false);
  auto rootLayer = Layer::Make();
  displayList->root()->addChild(rootLayer);

  auto shapeLayer1 = ShapeLayer::Make();
  shapeLayer1->setStrokeStyle(SolidColor::Make(Color::Black()));
  auto path1 = Path();
  path1.addRect(Rect::MakeXYWH(40, 40, 100, 140));
  shapeLayer1->setPath(path1);
  // rootLayer->addChild(shapeLayer1);
  auto bounds1 = shapeLayer1->getBounds();
  shapeLayer1->getGlobalMatrix().mapRect(&bounds1);

  auto shapeLayer2 = ShapeLayer::Make();
  shapeLayer2->setStrokeStyle(SolidColor::Make(Color::Black()));
  auto path2 = Path();
  path2.addRect(Rect::MakeXYWH(120, 20, 60, 220));
  shapeLayer2->setPath(path2);
  // rootLayer->addChild(shapeLayer2);
  auto bounds2 = shapeLayer2->getBounds();
  shapeLayer2->getGlobalMatrix().mapRect(&bounds2);

  auto shapeLayer3 = ShapeLayer::Make();
  shapeLayer3->setStrokeStyle(SolidColor::Make(Color::Black()));
  auto path3 = Path();
  path3.addRect(Rect::MakeXYWH(60, 80, 40, 60));
  shapeLayer3->setPath(path3);
  // rootLayer->addChild(shapeLayer3);
  auto bounds3 = shapeLayer3->getBounds();
  shapeLayer3->getGlobalMatrix().mapRect(&bounds3);

  auto shapeLayer4 = ShapeLayer::Make();
  shapeLayer4->setStrokeStyle(SolidColor::Make(Color::Black()));
  auto path4 = Path();
  path4.addRect(Rect::MakeXYWH(800, 40, 80, 100));
  shapeLayer4->setPath(path4);
  // rootLayer->addChild(shapeLayer4);
  auto bounds4 = shapeLayer4->getBounds();
  shapeLayer4->getGlobalMatrix().mapRect(&bounds4);

  auto shapeLayer5 = ShapeLayer::Make();
  shapeLayer5->setStrokeStyle(SolidColor::Make(Color::Black()));
  auto path5 = Path();
  path5.addRect(Rect::MakeXYWH(840, 110, 120, 130));
  shapeLayer5->setPath(path5);
  // rootLayer->addChild(shapeLayer5);
  auto bounds5 = shapeLayer5->getBounds();
  shapeLayer5->getGlobalMatrix().mapRect(&bounds5);

  auto shapeLayer6 = ShapeLayer::Make();
  shapeLayer6->setStrokeStyle(SolidColor::Make(Color::Black()));
  auto path6 = Path();
  path6.addRect(Rect::MakeXYWH(80, 460, 120, 180));
  shapeLayer6->setPath(path6);
  // rootLayer->addChild(shapeLayer6);
  auto bounds6 = shapeLayer6->getBounds();
  shapeLayer6->getGlobalMatrix().mapRect(&bounds6);

  auto shapeLayer7 = ShapeLayer::Make();
  shapeLayer7->setStrokeStyle(SolidColor::Make(Color::Black()));
  auto path7 = Path();
  path7.addRect(Rect::MakeXYWH(20, 600, 240, 100));
  shapeLayer7->setPath(path7);
  // rootLayer->addChild(shapeLayer7);
  auto bounds7 = shapeLayer7->getBounds();
  shapeLayer7->getGlobalMatrix().mapRect(&bounds7);

  auto shapeLayer8 = ShapeLayer::Make();
  shapeLayer8->setStrokeStyle(SolidColor::Make(Color::Black()));
  auto path8 = Path();
  path8.addRect(Rect::MakeXYWH(300, 500, 100, 140));
  shapeLayer8->setPath(path8);
  // rootLayer->addChild(shapeLayer8);
  auto bounds8 = shapeLayer8->getBounds();
  shapeLayer8->getGlobalMatrix().mapRect(&bounds8);

  auto shapeLayer9 = ShapeLayer::Make();
  shapeLayer9->setStrokeStyle(SolidColor::Make(Color::Black()));
  auto path9 = Path();
  path9.addRect(Rect::MakeXYWH(220, 460, 140, 50));
  shapeLayer9->setPath(path9);
  // rootLayer->addChild(shapeLayer9);
  auto bounds9 = shapeLayer9->getBounds();
  shapeLayer9->getGlobalMatrix().mapRect(&bounds9);

  auto shapeLayer10 = ShapeLayer::Make();
  shapeLayer10->setStrokeStyle(SolidColor::Make(Color::Black()));
  auto path10 = Path();
  path10.addRect(Rect::MakeXYWH(820, 420, 140, 200));
  shapeLayer10->setPath(path10);
  // rootLayer->addChild(shapeLayer10);
  auto bounds10 = shapeLayer10->getBounds();
  shapeLayer10->getGlobalMatrix().mapRect(&bounds10);

  auto shapeLayer11 = ShapeLayer::Make();
  shapeLayer11->setStrokeStyle(SolidColor::Make(Color::Black()));
  auto path11 = Path();
  path11.addRect(Rect::MakeXYWH(850, 540, 80, 40));
  shapeLayer11->setPath(path11);
  // rootLayer->addChild(shapeLayer11);
  auto bounds11 = shapeLayer11->getBounds();
  shapeLayer11->getGlobalMatrix().mapRect(&bounds11);

  displayList->render(surface.get());
  displayList->showDirtyRegions(true);

  rootLayer->removeChildren();
  rootLayer->addChild(shapeLayer1);
  rootLayer->addChild(shapeLayer2);
  rootLayer->addChild(shapeLayer3);

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DirtyRegionTest1"));

  rootLayer->removeChildren();
  rootLayer->addChild(shapeLayer1);
  rootLayer->addChild(shapeLayer2);
  rootLayer->addChild(shapeLayer3);
  rootLayer->addChild(shapeLayer4);
  rootLayer->addChild(shapeLayer5);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DirtyRegionTest2"));

  rootLayer->removeChildren();
  rootLayer->addChild(shapeLayer1);
  rootLayer->addChild(shapeLayer2);
  rootLayer->addChild(shapeLayer3);
  rootLayer->addChild(shapeLayer4);
  rootLayer->addChild(shapeLayer5);
  rootLayer->addChild(shapeLayer6);
  rootLayer->addChild(shapeLayer7);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DirtyRegionTest3"));

  rootLayer->removeChildren();
  rootLayer->addChild(shapeLayer1);
  rootLayer->addChild(shapeLayer2);
  rootLayer->addChild(shapeLayer3);
  rootLayer->addChild(shapeLayer4);
  rootLayer->addChild(shapeLayer5);
  rootLayer->addChild(shapeLayer6);
  rootLayer->addChild(shapeLayer7);
  rootLayer->addChild(shapeLayer8);
  rootLayer->addChild(shapeLayer9);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DirtyRegionTest4"));

  rootLayer->removeChildren();
  rootLayer->addChild(shapeLayer1);
  rootLayer->addChild(shapeLayer2);
  rootLayer->addChild(shapeLayer3);
  rootLayer->addChild(shapeLayer4);
  rootLayer->addChild(shapeLayer5);
  rootLayer->addChild(shapeLayer6);
  rootLayer->addChild(shapeLayer7);
  rootLayer->addChild(shapeLayer8);
  rootLayer->addChild(shapeLayer9);
  rootLayer->addChild(shapeLayer10);
  rootLayer->addChild(shapeLayer11);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DirtyRegionTest5"));

  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setAllowZoomBlur(true);
  displayList->setMaxTileCount(512);
  displayList->render(surface.get());
  // Clear the previous dirty regions.
  displayList->showDirtyRegions(false);
  displayList->showDirtyRegions(true);
  rootLayer->removeChildren();
  rootLayer->addChild(shapeLayer1);
  rootLayer->addChild(shapeLayer2);
  rootLayer->addChild(shapeLayer3);
  rootLayer->addChild(shapeLayer4);
  rootLayer->addChild(shapeLayer5);
  rootLayer->addChild(shapeLayer6);
  rootLayer->addChild(shapeLayer7);
  rootLayer->addChild(shapeLayer8);
  rootLayer->addChild(shapeLayer9);
  rootLayer->addChild(shapeLayer10);
  rootLayer->addChild(shapeLayer11);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DirtyRegionTest6"));

  // Clear the previous dirty regions.
  displayList->showDirtyRegions(false);
  displayList->showDirtyRegions(true);
  displayList->setContentOffset(-100, -300);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DirtyRegionTest7"));

  // Clear the previous dirty regions.
  displayList->showDirtyRegions(false);
  displayList->showDirtyRegions(true);
  displayList->setZoomScale(1.3f);
  displayList->setMaxTilesRefinedPerFrame(0);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DirtyRegionTest8"));

  // Clear the previous dirty regions.
  displayList->showDirtyRegions(false);
  displayList->showDirtyRegions(true);
  displayList->setMaxTilesRefinedPerFrame(INT_MAX);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DirtyRegionTest9"));

  // Clear the previous dirty regions.
  displayList->showDirtyRegions(false);
  displayList->showDirtyRegions(true);
  displayList->setContentOffset(250, 150);
  displayList->setZoomScale(0.5f);
  displayList->setMaxTilesRefinedPerFrame(0);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DirtyRegionTest10"));

  // Clear the previous dirty regions.
  displayList->showDirtyRegions(false);
  displayList->showDirtyRegions(true);
  displayList->setMaxTilesRefinedPerFrame(INT_MAX);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DirtyRegionTest11"));
}

TGFX_TEST(LayerTest, LayerVisible) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 100, 100);
  auto displayList = std::make_unique<DisplayList>();
  auto rootLayer = Layer::Make();
  displayList->root()->addChild(rootLayer);
  auto layer = ShapeLayer::Make();
  auto path = Path();
  path.addRect(Rect::MakeXYWH(0, 0, 100, 100));
  layer->setPath(path);
  layer->setFillStyle(SolidColor::Make(Color::Red()));
  layer->setVisible(true);
  rootLayer->addChild(layer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerVisible"));
  layer->setVisible(false);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerVisible1"));
  layer->setVisible(true);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerVisible"));
}

TGFX_TEST(LayerTest, BackgroundBlurStyleTest) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  Layer::SetDefaultAllowsGroupOpacity(true);
  auto displayList = std::make_unique<DisplayList>();
  displayList->showDirtyRegions(false);
  auto rootLayer = Layer::Make();
  displayList->root()->addChild(rootLayer);
  auto shapeLayer1 = ShapeLayer::Make();
  shapeLayer1->setFillStyle(SolidColor::Make(Color::FromRGBA(0, 0, 0, 2)));
  auto path1 = Path();
  path1.addRect(Rect::MakeXYWH(40.5f, 40.5f, 80.f, 80.f));
  shapeLayer1->setPath(path1);
  shapeLayer1->setMatrix(Matrix::MakeTrans(0.5f, 0.5f));
  shapeLayer1->setLayerStyles({BackgroundBlurStyle::Make(4, 4)});
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  auto imageLayer = ImageLayer::Make();
  imageLayer->setImage(image);
  rootLayer->addChildAt(imageLayer, 0);

  auto shapeLayer2 = ShapeLayer::Make();
  auto path2 = Path();
  path2.addRect(Rect::MakeXYWH(50, 20, 100, 100));
  shapeLayer2->setPath(path2);
  shapeLayer2->setFillStyle(
      Gradient::MakeLinear({50, 20}, {150, 120}, {{0.f, 0.f, 1.f, 1.f}, {0.f, 1.f, 0.f, 1.f}}));
  rootLayer->addChildAt(shapeLayer2, 0);

  auto layer2 = Layer::Make();
  layer2->addChild(shapeLayer1);
  layer2->setShouldRasterize(true);
  rootLayer->addChild(layer2);
  displayList->setZoomScale(2.0f);
  displayList->setContentOffset(-50, -50);
  displayList->setRenderMode(RenderMode::Direct);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/BackgroundBlurStyleTest1"));
  layer2->setBlendMode(BlendMode::Difference);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/BackgroundBlurStyleTest2"));
  surface->getCanvas()->clear();
  surface->getCanvas()->resetMatrix();
  layer2->draw(surface->getCanvas());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/BackgroundBlurStyleTest3"));
  auto maskLayer = ShapeLayer::Make();
  auto maskPath = Path();
  maskPath.addRect(Rect::MakeXYWH(80, 80, 200, 200));
  maskLayer->setPath(maskPath);
  maskLayer->setFillStyle(SolidColor::Make(Color::FromRGBA(0, 0, 0, 255)));
  imageLayer->setMask(maskLayer);
  rootLayer->addChild(maskLayer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/BackgroundBlurStyleTest4"));

  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->setMatrix(Matrix::MakeScale(2.0f, 2.0f));
  canvas->translate(-50, -50);
  layer2->draw(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/BackgroundBlurStyleTest5"));
}

TGFX_TEST(LayerTest, PartialBackgroundBlur) {
  ContextScope scope;
  auto context = scope.getContext();
  auto surface = Surface::Make(context, 300, 300);
  EXPECT_TRUE(context != nullptr);
  DisplayList displayList;
  auto rootLayer = Layer::Make();
  displayList.root()->addChild(rootLayer);
  auto background = ShapeLayer::Make();
  Path backgroundPath;
  backgroundPath.addRect(Rect::MakeXYWH(0, 0, 300, 300));
  background->setPath(backgroundPath);
  background->addFillStyle(Gradient::MakeRadial({150, 150}, 360, {Color::Red(), Color::Blue()}));
  rootLayer->addChild(background);
  auto solidLayer = SolidLayer::Make();
  solidLayer->setColor(Color::FromRGBA(0, 0, 0, 50));
  solidLayer->setWidth(200);
  solidLayer->setHeight(200);
  solidLayer->setMatrix(Matrix::MakeTrans(50, 50));
  solidLayer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  rootLayer->addChild(solidLayer);
  auto solidLayer2 = SolidLayer::Make();
  solidLayer2->setColor(Color::FromRGBA(0, 0, 0, 10));
  solidLayer2->setWidth(50);
  solidLayer2->setHeight(50);
  solidLayer2->setMatrix(Matrix::MakeTrans(100, 100));
  rootLayer->addChild(solidLayer2);
  displayList.setRenderMode(RenderMode::Partial);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PartialBackgroundBlur"));
  solidLayer2->removeFromParent();
  rootLayer->addChild(solidLayer2);
  EXPECT_TRUE(displayList.root()->bitFields.dirtyDescendents);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PartialBackgroundBlur_partial"));
  solidLayer2->setMatrix(Matrix::MakeTrans(120, 120));
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PartialBackgroundBlur_move"));
}

TGFX_TEST(LayerTest, PartialInnerShadow) {
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
  shapeLayer->setFillStyle(SolidColor::Make(Color::FromRGBA(255, 255, 255, 255)));
  shapeLayer->setLineWidth(1.0f);
  rootLayer->addChild(shapeLayer);

  auto innerShadowStyle = InnerShadowStyle::Make(10, 10, 0, 0, Color::Black());
  displayList.setContentOffset(-5, -5);
  displayList.setRenderMode(RenderMode::Tiled);
  displayList.render(surface.get());

  shapeLayer->setLayerStyles({});
  shapeLayer->setLayerStyles({innerShadowStyle});
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PartialInnerShadow"));
}

TGFX_TEST(LayerTest, PartialDrawLayer) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList displayList;
  auto surface = Surface::Make(context, 200, 200);
  auto rootLayer = Layer::Make();
  rootLayer->setMatrix(Matrix::MakeTrans(40, 40));
  displayList.root()->addChild(rootLayer);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  auto imageLayer = ImageLayer::Make();
  imageLayer->setImage(image);
  rootLayer->addChild(imageLayer);
  auto shapeLayer = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeXYWH(0, 0, 100, 100));
  shapeLayer->setPath(path);
  shapeLayer->setFillStyle(SolidColor::Make(Color::FromRGBA(255, 255, 255, 50)));
  shapeLayer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  rootLayer->addChild(shapeLayer);
  auto layerInvisible = SolidLayer::Make();
  layerInvisible->setColor(Color::FromRGBA(0, 0, 0, 255));
  layerInvisible->setWidth(100);
  layerInvisible->setHeight(100);
  layerInvisible->setMatrix(Matrix::MakeTrans(100, 100));
  layerInvisible->setShouldRasterize(true);
  rootLayer->addChild(layerInvisible);
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->save();
  canvas->rotate(30, 45, 45);
  canvas->clipRect(Rect::MakeXYWH(0, 0, 110, 110));
  canvas->scale(2.0f, 1.0f);
  canvas->translate(20, 20);
  rootLayer->draw(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PartialDrawLayer"));
  EXPECT_EQ(layerInvisible->rasterizedContent, nullptr);
  canvas->restore();

  canvas->clear();
  canvas->rotate(30, 45, 45);
  canvas->clipRect(Rect::MakeXYWH(0, 0, 110, 110));
  canvas->scale(2.0f, 1.0f);
  canvas->translate(20, 20);
  shapeLayer->draw(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PartialDrawLayer_shapeLayer"));
}

TGFX_TEST(LayerTest, DropShadowDirtyRect) {
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
  shapeLayer->setFillStyle(SolidColor::Make(Color::FromRGBA(255, 0, 0, 255)));
  shapeLayer->setLayerStyles({DropShadowStyle::Make(10, 10, 0, 0, Color::Black())});
  rootLayer->addChild(shapeLayer);
  shapeLayer->setMatrix(Matrix::MakeRotate(-120));
  displayList.setContentOffset(50, 150);
  displayList.render(surface.get());
  shapeLayer->setMatrix(Matrix::MakeRotate(-121));
  displayList.showDirtyRegions(true);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DropShadowDirtyRect"));
}

TGFX_TEST(LayerTest, ContourTest) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  auto baseLayer = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeXYWH(50, 50, 100, 100));
  baseLayer->setPath(path);
  baseLayer->setFillStyle(SolidColor::Make(Color::FromRGBA(255, 0, 0, 255)));

  auto layerOutOfBase = ShapeLayer::Make();
  Path pathOutOfBase;
  pathOutOfBase.addRect(Rect::MakeXYWH(80, 50, 100, 80));
  layerOutOfBase->setPath(pathOutOfBase);
  layerOutOfBase->setFillStyle(SolidColor::Make(Color::FromRGBA(0, 255, 0, 128)));
  baseLayer->addChild(layerOutOfBase);

  auto layerOutOfBase2 = ShapeLayer::Make();
  layerOutOfBase2->setPath(pathOutOfBase);
  layerOutOfBase2->setFillStyle(SolidColor::Make(Color::FromRGBA(0, 255, 255, 128)));
  layerOutOfBase2->setMatrix(Matrix::MakeRotate(30, 80, 90));
  baseLayer->addChild(layerOutOfBase2);

  auto rRectLayerInBase = ShapeLayer::Make();
  Path rRectPath;
  rRectPath.addRRect(RRect{Rect::MakeXYWH(60, 60, 40, 40), Point::Make(20, 20)});
  rRectLayerInBase->setPath(rRectPath);
  rRectLayerInBase->setFillStyle(SolidColor::Make(Color::FromRGBA(255, 255, 0, 128)));
  baseLayer->addChild(rRectLayerInBase);

  auto rRectLayerOutBase = ShapeLayer::Make();
  Path rRectPath2;
  rRectPath2.addRRect(RRect{Rect::MakeXYWH(60, 60, 100, 100), Point::Make(20, 20)});
  rRectLayerOutBase->setPath(rRectPath2);
  rRectLayerOutBase->setFillStyle(SolidColor::Make(Color::FromRGBA(255, 0, 255, 128)));
  baseLayer->addChild(rRectLayerOutBase);
  auto rRectLayerOutBase2 = ShapeLayer::Make();
  rRectLayerOutBase2->setPath(rRectPath2);
  rRectLayerOutBase2->setFillStyle(SolidColor::Make(Color::FromRGBA(0, 0, 255, 128)));
  baseLayer->addChild(rRectLayerOutBase2);

  auto layerInBase = ShapeLayer::Make();
  Path pathInBase;
  pathInBase.addRect(Rect::MakeXYWH(80, 80, 30, 30));
  layerInBase->setPath(pathInBase);
  layerInBase->setFillStyle(SolidColor::Make(Color::FromRGBA(0, 0, 255, 128)));
  layerInBase->setMatrix(Matrix::MakeRotate(30, 60, 60));
  baseLayer->addChild(layerInBase);

  auto pathOutBase = ShapeLayer::Make();
  Path pathOutBasePath;
  pathOutBasePath.moveTo(200, 200);
  pathOutBasePath.cubicTo(250, 70, 250, 150, 200, 150);
  pathOutBase->setPath(pathOutBasePath);
  pathOutBase->setLineWidth(2);
  pathOutBase->setStrokeStyle(SolidColor::Make(Color::FromRGBA(0, 0, 0, 255)));
  baseLayer->addChild(pathOutBase);

  ContourContext contourContext;
  Canvas contourCanvas = Canvas(&contourContext);
  DrawArgs drawArgs = DrawArgs(nullptr);
  drawArgs.drawMode = DrawMode::Contour;
  baseLayer->drawLayer(drawArgs, &contourCanvas, 1.0, BlendMode::SrcOver);
  auto picture = contourContext.finishRecordingAsPicture();

  EXPECT_TRUE(picture != nullptr);
  EXPECT_TRUE(picture->drawCount == 6);
  EXPECT_TRUE(contourContext.contourBounds.size() == 3);
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->drawPicture(picture);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/ContourTest"));
}

TGFX_TEST(LayerTest, NotRectBackgroundBlur) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto backgroundLayer = ShapeLayer::Make();
  Path backgroundPath;
  backgroundPath.addRect(Rect::MakeXYWH(0, 0, 200, 200));
  backgroundLayer->setPath(backgroundPath);
  backgroundLayer->addFillStyle(
      Gradient::MakeRadial({100, 100}, 100, {Color::Red(), Color::Blue()}));
  DisplayList displayList;
  displayList.root()->addChild(backgroundLayer);
  auto layer = ShapeLayer::Make();
  Path path;
  path.addOval(Rect::MakeXYWH(50, 50, 100, 100));
  layer->setPath(path);
  layer->setFillStyle(SolidColor::Make(Color::FromRGBA(255, 0, 0, 10)));
  layer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  displayList.root()->addChild(layer);
  layer->draw(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/NotRectBackgroundBlur"));
}

TGFX_TEST(LayerTest, DiffFilterModeImagePattern) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto image = MakeImage("resources/apitest/image_as_mask.png");
  EXPECT_TRUE(image != nullptr);
  auto shapeLayer = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeWH(image->width(), image->height()));
  shapeLayer->setPath(path);
  auto pattern = ImagePattern::Make(image, TileMode::Decal, TileMode::Decal,
                                    SamplingOptions(FilterMode::Linear, FilterMode::Nearest));
  DisplayList displayList;
  displayList.root()->addChild(shapeLayer);
  shapeLayer->setFillStyle(pattern);
  displayList.setZoomScale(0.3f);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DiffFilterModeImagePattern -- zoomOut"));
  displayList.setZoomScale(1.5f);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DiffFilterModeImagePattern -- zoomIn"));
}

TGFX_TEST(LayerTest, TemporaryOffscreenImage) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto image = MakeImage("resources/apitest/image_as_mask.png");
  EXPECT_TRUE(image != nullptr);
  auto shapeLayer = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeWH(image->width(), image->height()));
  shapeLayer->setPath(path);
  auto pattern = ImagePattern::Make(image, TileMode::Decal, TileMode::Decal,
                                    SamplingOptions(FilterMode::Linear, FilterMode::Nearest));
  auto filter = DropShadowFilter::Make(-10, -10, 5, 5, Color::Black());
  DisplayList displayList;
  displayList.root()->addChild(shapeLayer);
  shapeLayer->setFillStyle(pattern);
  shapeLayer->setFilters({filter});

  auto glassLayer = ShapeLayer::Make();
  Path glassPath;
  glassPath.addRRect(RRect{Rect::MakeXYWH(10, 10, 80, 80), Point::Make(40, 40)});
  glassLayer->setPath(glassPath);
  glassLayer->setFillStyle(SolidColor::Make(Color::FromRGBA(255, 255, 255, 50)));
  glassLayer->setLayerStyles({BackgroundBlurStyle::Make(6, 6)});
  shapeLayer->addChild(glassLayer);
  displayList.setZoomScale(2.f);
  displayList.setContentOffset(-50, -50);
  displayList.render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/TemporaryOffscreenImage_pic"));

  displayList.root()->addChild(glassLayer);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/TemporaryOffscreenImage_image1"));

  shapeLayer->setFilters({});
  shapeLayer->setAlpha(0.8f);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/TemporaryOffscreenImage_image2"));
}

TGFX_TEST(LayerTest, PassThrough_Test) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto shapeLayer = ShapeLayer::Make();
  auto value = Layer::DefaultAllowsGroupOpacity();
  Layer::SetDefaultAllowsGroupOpacity(true);
  Path path;
  path.addRect(Rect::MakeXYWH(-40, -40, 100, 100));
  shapeLayer->setPath(path);
  auto image = MakeImage("resources/apitest/image_as_mask.png");
  EXPECT_TRUE(image != nullptr);
  auto pattern = ImagePattern::Make(image, TileMode::Decal, TileMode::Decal,
                                    SamplingOptions(FilterMode::Linear, FilterMode::Nearest));
  pattern->setMatrix(Matrix::MakeTrans(-40, -40));
  shapeLayer->setFillStyle(pattern);
  shapeLayer->setMatrix(Matrix::MakeRotate(45, 30, 30));

  auto childLayer = ShapeLayer::Make();
  Path childPath;
  childPath.addRect(Rect::MakeXYWH(25, 25, 50, 50));
  childLayer->setPath(childPath);
  childLayer->setFillStyle(SolidColor::Make(Color::FromRGBA(0, 0, 255, 255)));
  childLayer->setAlpha(0.5f);
  shapeLayer->addChild(childLayer);

  auto childLayer2 = ShapeLayer::Make();
  Path childPath2;
  childPath2.addRect(Rect::MakeXYWH(10, 10, 30, 30));
  childLayer2->setPath(childPath2);
  childLayer2->setFillStyle(SolidColor::Make(Color::FromRGBA(0, 255, 0, 255)));
  childLayer2->setBlendMode(BlendMode::Exclusion);
  childLayer->addChild(childLayer2);

  DisplayList displayList;
  displayList.root()->addChild(shapeLayer);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PassThrough_Test"));

  PictureRecorder recorder;
  auto newCanvas = recorder.beginRecording();
  newCanvas->rotate(45, 30, 30);
  shapeLayer->draw(newCanvas);
  auto picture = recorder.finishRecordingAsPicture();
  canvas->clear();
  canvas->drawPicture(picture);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PassThrough_Test_record"));
  Layer::SetDefaultAllowsGroupOpacity(value);
}

static inline Matrix3D MakePerspectiveMatrix() {
  auto perspectiveMatrix = Matrix3D::I();
  constexpr float eyeDistance = 1200.f;
  perspectiveMatrix.setRowColumn(3, 2, -1.f / eyeDistance);
  return perspectiveMatrix;
}

TGFX_TEST(LayerTest, Matrix) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 200);
  auto canvas = surface->getCanvas();
  canvas->clear();

  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);

  auto backLayer = ImageLayer::Make();
  auto backImage = MakeImage("resources/assets/HappyNewYear.png");
  backLayer->setImage(backImage);
  backLayer->setMatrix(Matrix::MakeScale(0.5f, 0.5f));
  displayList->root()->addChild(backLayer);

  auto contentLayer = SolidLayer::Make();
  contentLayer->setColor(Color::FromRGBA(151, 153, 46, 255));
  auto contentLayerSize = Size::Make(360.f, 320.f);
  contentLayer->setWidth(contentLayerSize.width);
  contentLayer->setHeight(contentLayerSize.height);
  {
    auto anchor = Point::Make(0.3f, 0.3f);
    auto offsetToAnchorMatrix = Matrix3D::MakeTranslate(-anchor.x * contentLayerSize.width,
                                                        -anchor.y * contentLayerSize.height, 0.f);
    auto invOffsetToAnchorMatrix = Matrix3D::MakeTranslate(anchor.x * contentLayerSize.width,
                                                           anchor.y * contentLayerSize.height, 0.f);
    auto modelMatrix = Matrix3D::MakeRotate({0.f, 1.f, 0.f}, -45.f);
    auto perspectiveMatrix = MakePerspectiveMatrix();
    auto origin = Point::Make(120, 40);
    auto originTranslateMatrix = Matrix3D::MakeTranslate(origin.x, origin.y, 0.f);
    auto transformMatrix = originTranslateMatrix * invOffsetToAnchorMatrix * perspectiveMatrix *
                           modelMatrix * offsetToAnchorMatrix;
    contentLayer->setMatrix3D(transformMatrix);
  }
  backLayer->addChild(contentLayer);

  auto shadowFilter = DropShadowFilter::Make(-20, -20, 0, 0, Color::Green());
  auto image = MakeImage("resources/apitest/imageReplacement.jpg");
  auto imageLayer = ImageLayer::Make();
  imageLayer->setImage(image);
  imageLayer->setFilters({shadowFilter});
  auto imageMatrix3D = Matrix3D::I();
  {
    auto imageSize =
        Size::Make(static_cast<float>(image->width()), static_cast<float>(image->height()));
    auto anchor = Point::Make(0.5f, 0.5f);
    // The default anchor point for layer filters is the layer origin. Image layers by default align
    // their content to the layer origin. When using the center point of the image content's
    // normalized width and height coordinates as the anchor point, additional matrix processing is
    // required.
    auto offsetToAnchorMatrix =
        Matrix3D::MakeTranslate(-anchor.x * imageSize.width, -anchor.y * imageSize.height, 0.f);
    auto invOffsetToAnchorMatrix =
        Matrix3D::MakeTranslate(anchor.x * imageSize.width, anchor.y * imageSize.height, 0.f);
    // During continuous model transformations, if transforming based on the world coordinate system
    // requires appending matrix elements on the left side of matrix multiplication (using
    // post-prefix functions), while transforming based on the model coordinate system appends on
    // the right side (using pre-prefix functions)
    // The following operation order is derived to ensure alignment with CSS effects under various
    // transformations with identical values
    auto modelMatrix = Matrix3D::MakeScale(2.f, 2.f, 1.f);
    constexpr float skewXDegrees = -15.f;
    constexpr float skewYDegrees = -15.f;
    modelMatrix.postSkewXY(tanf(DegreesToRadians(skewXDegrees)),
                           tanf(DegreesToRadians(skewYDegrees)));
    modelMatrix.postRotate({0.f, 0.f, 1.f}, 45.f);
    modelMatrix.preRotate({1.f, 0.f, 0.f}, 45.f);
    modelMatrix.preRotate({0.f, 1.f, 0.f}, 45.f);
    modelMatrix.postTranslate(0.f, 0.f, 100.f);
    auto perspectiveMatrix = MakePerspectiveMatrix();
    // The origin coordinates of the layer in the local coordinate system when no model
    // transformation (excluding XY translation) is applied
    auto origin = Point::Make(125, 105);
    auto originTranslateMatrix = Matrix3D::MakeTranslate(origin.x, origin.y, 0.f);
    imageMatrix3D = originTranslateMatrix * invOffsetToAnchorMatrix * perspectiveMatrix *
                    modelMatrix * offsetToAnchorMatrix;
  }
  contentLayer->addChild(imageLayer);

  imageLayer->setMatrix3D(imageMatrix3D);
  displayList->render(surface.get());
  EXPECT_EQ(imageLayer->getBounds(contentLayer.get()), Rect::MakeLTRB(65, 0, 298, 281));
  EXPECT_EQ(imageLayer->getBounds(displayList->root()), Rect::MakeLTRB(99, 15, 190, 162));
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/Matrix_3D"));

  auto affineMatrix = Matrix::MakeTrans(50, 50);
  imageLayer->setMatrix(affineMatrix);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/Matrix_3D_2D"));

  contentLayer->setTransformStyle(TransformStyle::Preserve3D);
  imageLayer->setMatrix3D(imageMatrix3D);
  imageLayer->setShouldRasterize(true);
  EXPECT_TRUE(imageLayer->matrix().isIdentity());
  auto rect = Rect::MakeXYWH(50, 50, 200, 100);
  Path path = {};
  path.addRoundRect(rect, 20, 20);
  auto shaperLayer = ShapeLayer::Make();
  shaperLayer->setPath(path);
  shaperLayer->setFillStyle(SolidColor::Make(Color::FromRGBA(0, 0, 255, 128)));
  shaperLayer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  {
    // Verify the correctness of ShaperLayer's effect when the internal matrix is non-zero
    auto layerSize = Size::Make(300.f, 200.f);
    auto anchor = Point::Make(0.5f, 0.5f);
    auto offsetToAnchorMatrix =
        Matrix3D::MakeTranslate(-anchor.x * layerSize.width, -anchor.y * layerSize.height, 0.f);
    auto invOffsetToAnchorMatrix =
        Matrix3D::MakeTranslate(anchor.x * layerSize.width, anchor.y * layerSize.height, 0.f);
    auto modelMatrix = Matrix3D::MakeRotate({0.f, 1.f, 0.f}, 45.f);
    auto perspectiveMatrix = MakePerspectiveMatrix();
    auto origin = Point::Make(0, 0);
    auto originTranslateMatrix = Matrix3D::MakeTranslate(origin.x, origin.y, 0.f);
    auto transformMatrix = originTranslateMatrix * invOffsetToAnchorMatrix * perspectiveMatrix *
                           modelMatrix * offsetToAnchorMatrix;
    shaperLayer->setMatrix3D(transformMatrix);
  }
  displayList->root()->addChild(shaperLayer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/Matrix_3D_2D_3D"));

  {
    auto anchor = Point::Make(0.3f, 0.3f);
    auto offsetToAnchorMatrix = Matrix3D::MakeTranslate(-anchor.x * contentLayerSize.width,
                                                        -anchor.y * contentLayerSize.height, 0.f);
    auto invOffsetToAnchorMatrix = Matrix3D::MakeTranslate(anchor.x * contentLayerSize.width,
                                                           anchor.y * contentLayerSize.height, 0.f);
    auto modelMatrix = Matrix3D::MakeRotate({0.f, 1.f, 0.f}, -45.f);
    modelMatrix.postTranslate(0.f, 0.f, 1200.f);
    auto perspectiveMatrix = MakePerspectiveMatrix();
    auto origin = Point::Make(120, 40);
    auto originTranslateMatrix = Matrix3D::MakeTranslate(origin.x, origin.y, 0.f);
    auto transformMatrix = originTranslateMatrix * invOffsetToAnchorMatrix * perspectiveMatrix *
                           modelMatrix * offsetToAnchorMatrix;
    contentLayer->setMatrix3D(transformMatrix);
  }
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/Matrix_Behind_Viewer"));
}

}  // namespace tgfx