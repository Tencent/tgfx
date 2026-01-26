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
#include "layers/SubtreeCache.h"
#include "layers/TileCache.h"
#include "layers/compositing3d/Layer3DContext.h"
#include "layers/contents/ComposeContent.h"
#include "layers/contents/RRectsContent.h"
#include "layers/contents/RectsContent.h"
#include "layers/contents/TextContent.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/LayerRecorder.h"
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
  rectShape->setFillStyle(ShapeStyle::Make(Color::Red()));
  auto strokeStyle = ShapeStyle::Make(Color::Black());
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
  lineShape->setStrokeStyle(ShapeStyle::Make(Color::Red()));
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
  lineShapeChild->setStrokeStyle(ShapeStyle::Make(Color::Red()));
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
  lineBoundsShape->setStrokeStyle(ShapeStyle::Make(Color::Green()));
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
    std::shared_ptr<Shader> shader = nullptr;
    std::vector<Color> colors = {{0.f, 0.f, 1.f, 1.f}, {0.f, 1.f, 0.f, 1.f}};
    switch (i) {
      case 0:
        shader = Shader::MakeLinearGradient({rect.left, rect.top}, {rect.right, rect.top}, colors);
        break;
      case 1:
        shader = Shader::MakeRadialGradient({rect.centerX(), rect.centerY()}, rect.width() / 2.0f,
                                            colors);
        break;
      case 2:
        shader = Shader::MakeImageShader(MakeImage("resources/apitest/imageReplacement.png"),
                                         TileMode::Repeat, TileMode::Mirror);
        shader = shader->makeWithMatrix(Matrix::MakeTrans(-25, rect.top - 70));
        break;
      default:
        break;
    }
    shapeLayer->addFillStyle(ShapeStyle::Make(shader, 0.8f));

    // stroke style
    shapeLayer->setLineWidth(10.0f);
    shapeLayer->setLineCap(LineCap::Butt);
    shapeLayer->setLineJoin(LineJoin::Miter);
    auto strokeStyle = ShapeStyle::Make(Color::Red());
    shapeLayer->setStrokeStyle(strokeStyle);
    strokeStyle = ShapeStyle::Make(Color{0.0f, 1.0f, 0.0f, 0.5f}, BlendMode::Lighten);
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
  shapeLayer->setFillStyle(ShapeStyle::Make(Color::Red()));
  auto strokeColor = ShapeStyle::Make(Color{0.0f, 1.0f, 0.0f, 0.5f});
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
  rect1->setFillStyle(ShapeStyle::Make(Color::FromRGBA(123, 77, 77)));
  auto rect2 = ShapeLayer::Make();
  rect2->setPath(rect);
  rect2->setMatrix(Matrix::MakeTrans(100, 100));
  rect2->setFillStyle(ShapeStyle::Make(Color::FromRGBA(219, 32, 32)));
  root->addChild(rect1);
  root->addChild(rect2);
  displayList.root()->addChild(root);
  rect1->setBlendMode(BlendMode::SoftLight);
  rect2->setBlendMode(BlendMode::Screen);
  displayList.render(surface.get(), false);

  root->setMatrix(Matrix::MakeTrans(400, 50));
  displayList.setRenderMode(RenderMode::Direct);
  displayList.render(surface.get(), false);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PassThoughAndNormal"));
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
  shapeLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 0, 0)));
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
  auto imageLayerBounds = imageLayer->getBounds(displayList->root(), true);
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
  auto fillStyle = ShapeStyle::Make(Color::FromRGBA(255, 0, 0, 127));
  shaperLayer->setFillStyle(fillStyle);
  shaperLayer->setMatrix(Matrix::MakeTrans(100.0f, 0.0f) * Matrix::MakeScale(2.0f, 2.0f));
  rootLayer->addChild(shaperLayer);
  auto shaperLayerBounds = shaperLayer->getBounds(displayList->root(), true);
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
  auto textLayerBounds = textLayer->getBounds(displayList->root(), true);
  printf("textLayerBounds: (%f, %f, %f, %f)\n", textLayerBounds.left, textLayerBounds.top,
         textLayerBounds.right, textLayerBounds.bottom);

  auto shaperLayer2 = ShapeLayer::Make();
  shaperLayer2->setName("shaper_layer2");
  shaperLayer2->setMatrix(Matrix::MakeTrans(550.0f, 150.0f) * Matrix::MakeRotate(45.0f));
  auto rect2 = Rect::MakeXYWH(0, 0, 80, 80);
  Path path2 = {};
  path2.addRect(rect2);
  shaperLayer2->setPath(path2);
  auto fillStyle2 = ShapeStyle::Make(Color::FromRGBA(175, 27, 193, 255));
  shaperLayer2->setFillStyle(fillStyle2);
  rootLayer->addChild(shaperLayer2);
  auto shaperLayer2Bounds = shaperLayer2->getBounds(displayList->root(), true);
  printf("shaperLayer2Bounds: (%f, %f, %f, %f)\n", shaperLayer2Bounds.left, shaperLayer2Bounds.top,
         shaperLayer2Bounds.right, shaperLayer2Bounds.bottom);

  auto shapeLayerInvisibleFill = ShapeLayer::Make();
  auto rectPath1 = tgfx::Path();
  rectPath1.addRect({100, 300, 200, 375});
  shapeLayerInvisibleFill->setPath(rectPath1);
  shapeLayerInvisibleFill->setName("shapeLayerInvisibleFill");
  auto fillStyle0 = ShapeStyle::Make(Color::FromRGBA(130, 182, 41, 0));
  shapeLayerInvisibleFill->setFillStyle(fillStyle0);
  rootLayer->addChild(shapeLayerInvisibleFill);
  auto shapeLayerInvisibleFillBounds =
      shapeLayerInvisibleFill->getBounds(displayList->root(), true);
  printf("shapeLayerInvisibleFillBounds: (%f, %f, %f, %f)\n", shapeLayerInvisibleFillBounds.left,
         shapeLayerInvisibleFillBounds.top, shapeLayerInvisibleFillBounds.right,
         shapeLayerInvisibleFillBounds.bottom);

  auto shapeLayerInvisibleStroke = ShapeLayer::Make();
  auto rectPath2 = tgfx::Path();
  rectPath2.addRect({150, 320, 250, 395});
  shapeLayerInvisibleStroke->setPath(rectPath2);
  shapeLayerInvisibleStroke->setName("shapeLayerInvisibleStroke");
  shapeLayerInvisibleStroke->addStrokeStyle(
      tgfx::ShapeStyle::Make(tgfx::Color::FromRGBA(130, 182, 41, 0)));
  rootLayer->addChild(shapeLayerInvisibleStroke);

  auto shapeLayerInvisibleStrokeBounds =
      shapeLayerInvisibleStroke->getBounds(displayList->root(), true);
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
  auto fillStyle1 = ShapeStyle::Make(Color::FromRGBA(255, 0, 0, 127));
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
  auto fillStyle2 = ShapeStyle::Make(Color::FromRGBA(127, 255, 0, 127));
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

TGFX_TEST(LayerTest, HitTestPointWithStroke) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  auto canvas = surface->getCanvas();
  auto displayList = std::make_unique<DisplayList>();

  // Test stroke hit test for ShapeLayer with Path (triangle)
  auto pathLayer = ShapeLayer::Make();
  Path trianglePath = {};
  trianglePath.moveTo(100, 50);
  trianglePath.lineTo(150, 150);
  trianglePath.lineTo(50, 150);
  trianglePath.close();
  pathLayer->setPath(trianglePath);
  pathLayer->setStrokeStyle(ShapeStyle::Make(Color::Red()));
  pathLayer->setLineWidth(20.0f);
  displayList->root()->addChild(pathLayer);

  // Test stroke hit test for ShapeLayer with Rect
  auto rectLayer = ShapeLayer::Make();
  Path rectPath = {};
  rectPath.addRect(Rect::MakeXYWH(200, 50, 100, 100));
  rectLayer->setPath(rectPath);
  rectLayer->setStrokeStyle(ShapeStyle::Make(Color::Green()));
  rectLayer->setLineWidth(20.0f);
  displayList->root()->addChild(rectLayer);

  // Test stroke hit test for ShapeLayer with RRect
  auto rRectLayer = ShapeLayer::Make();
  Path rRectPath = {};
  RRect rRect = {};
  rRect.setRectXY(Rect::MakeXYWH(50, 200, 100, 100), 20.0f, 20.0f);
  rRectPath.addRRect(rRect);
  rRectLayer->setPath(rRectPath);
  rRectLayer->setStrokeStyle(ShapeStyle::Make(Color::Blue()));
  rRectLayer->setLineWidth(20.0f);
  displayList->root()->addChild(rRectLayer);

  // Test stroke hit test for ShapeLayer with Shape (oval)
  auto shapeLayer = ShapeLayer::Make();
  Path shapePath = {};
  shapePath.addOval(Rect::MakeXYWH(200, 200, 100, 100));
  auto shape = Shape::MakeFrom(shapePath);
  shapeLayer->setShape(shape);
  shapeLayer->setStrokeStyle(ShapeStyle::Make(Color::FromRGBA(255, 165, 0)));
  shapeLayer->setLineWidth(20.0f);
  displayList->root()->addChild(shapeLayer);

  displayList->render(surface.get());

  // Draw test points
  auto paint = Paint();
  paint.setColor(Color::White());
  paint.setStyle(PaintStyle::Fill);

  // Test points outside the original shape but inside the stroke area (stroke width = 20, half = 10)
  // These points should hit the stroke but would miss without stroke consideration.

  // P1: On triangle left edge stroke area - original edge is from (50,150) to (100,50)
  Point p1 = {65.0f, 110.0f};
  canvas->drawCircle(p1.x, p1.y, 3.0f, paint);
  EXPECT_TRUE(pathLayer->hitTestPoint(p1.x, p1.y, true));

  // P2: Further left of triangle edge, outside stroke area
  Point p2 = {45.0f, 110.0f};
  canvas->drawCircle(p2.x, p2.y, 3.0f, paint);
  EXPECT_FALSE(pathLayer->hitTestPoint(p2.x, p2.y, true));

  // P3: Left of rect, in stroke area (190, 100) - original rect left is at x=200
  Point p3 = {192.0f, 100.0f};
  canvas->drawCircle(p3.x, p3.y, 3.0f, paint);
  EXPECT_TRUE(rectLayer->hitTestPoint(p3.x, p3.y, true));

  // P4: Further left of rect, outside stroke area
  Point p4 = {185.0f, 100.0f};
  canvas->drawCircle(p4.x, p4.y, 3.0f, paint);
  EXPECT_FALSE(rectLayer->hitTestPoint(p4.x, p4.y, true));

  // P5: Left of rRect, in stroke area (42, 250) - original rRect left is at x=50
  Point p5 = {42.0f, 250.0f};
  canvas->drawCircle(p5.x, p5.y, 3.0f, paint);
  EXPECT_TRUE(rRectLayer->hitTestPoint(p5.x, p5.y, true));

  // P6: Further left of rRect, outside stroke area
  Point p6 = {35.0f, 250.0f};
  canvas->drawCircle(p6.x, p6.y, 3.0f, paint);
  EXPECT_FALSE(rRectLayer->hitTestPoint(p6.x, p6.y, true));

  // P7: Above oval, in stroke area (250, 192) - original oval top is at y=200
  Point p7 = {250.0f, 192.0f};
  canvas->drawCircle(p7.x, p7.y, 3.0f, paint);
  EXPECT_TRUE(shapeLayer->hitTestPoint(p7.x, p7.y, true));

  // P8: Further above oval, outside stroke area
  Point p8 = {250.0f, 185.0f};
  canvas->drawCircle(p8.x, p8.y, 3.0f, paint);
  EXPECT_FALSE(shapeLayer->hitTestPoint(p8.x, p8.y, true));

  // Test points inside the original shape but not on stroke (stroke only, no fill)
  // P9: Inside triangle center - should NOT hit (no fill, only stroke)
  Point p9 = {100.0f, 120.0f};
  canvas->drawCircle(p9.x, p9.y, 3.0f, paint);
  EXPECT_FALSE(pathLayer->hitTestPoint(p9.x, p9.y, true));

  // P10: Inside rect center - should NOT hit (no fill, only stroke)
  Point p10 = {250.0f, 100.0f};
  canvas->drawCircle(p10.x, p10.y, 3.0f, paint);
  EXPECT_FALSE(rectLayer->hitTestPoint(p10.x, p10.y, true));

  // P11: Inside rRect center - should NOT hit (no fill, only stroke)
  Point p11 = {100.0f, 250.0f};
  canvas->drawCircle(p11.x, p11.y, 3.0f, paint);
  EXPECT_FALSE(rRectLayer->hitTestPoint(p11.x, p11.y, true));

  // P12: Inside oval center - should NOT hit (no fill, only stroke)
  Point p12 = {250.0f, 250.0f};
  canvas->drawCircle(p12.x, p12.y, 3.0f, paint);
  EXPECT_FALSE(shapeLayer->hitTestPoint(p12.x, p12.y, true));

  // Test invisible (transparent) stroke - should hit in non-precise mode, miss in precise mode
  auto invisibleStrokeLayer = ShapeLayer::Make();
  Path invisiblePath = {};
  invisiblePath.addRect(Rect::MakeXYWH(320, 50, 60, 60));
  invisibleStrokeLayer->setPath(invisiblePath);
  invisibleStrokeLayer->setStrokeStyle(ShapeStyle::Make(Color::Transparent()));
  invisibleStrokeLayer->setLineWidth(20.0f);
  displayList->root()->addChild(invisibleStrokeLayer);

  // P13: On invisible stroke area - should hit in non-precise mode (bounding box)
  Point p13 = {312.0f, 80.0f};
  canvas->drawCircle(p13.x, p13.y, 3.0f, paint);
  EXPECT_TRUE(invisibleStrokeLayer->hitTestPoint(p13.x, p13.y, false));
  // P13: Same point - should NOT hit in precise mode (transparent stroke is invisible)
  EXPECT_FALSE(invisibleStrokeLayer->hitTestPoint(p13.x, p13.y, true));

  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/HitTestPointWithStroke"));
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
  auto imageLayerBounds = imageLayer->getBounds(displayList->root(), true);
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
  auto fillStyle = ShapeStyle::Make(Color::FromRGBA(127, 255, 0, 127));
  shaperLayer->setFillStyle(fillStyle);
  childLayer->addChild(shaperLayer);
  parentLayer->addChild(childLayer);
  auto shaperLayerBounds = shaperLayer->getBounds(displayList->root(), true);
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
  auto textLayerBounds = textLayer->getBounds(displayList->root(), true);
  printf("textLayerBounds: (%f, %f, %f, %f)\n", textLayerBounds.left, textLayerBounds.top,
         textLayerBounds.right, textLayerBounds.bottom);

  auto rootLayerBounds = rootLayer->getBounds(displayList->root(), true);
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

TGFX_TEST(LayerTest, LargeScale) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  auto shapeLayer = ShapeLayer::Make();
  Path path = {};
  path.addRect(Rect::MakeWH(10000, 10000));
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  auto imageShader = Shader::MakeImageShader(image)->makeWithMatrix(Matrix::MakeTrans(-20, -20));
  shapeLayer->setFillStyle(ShapeStyle::Make(imageShader));
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
  auto layerShader =
      Shader::MakeDiamondGradient(Point::Make(100, 50), 50, {Color::Red(), Color::Blue()}, {0, 1});
  layer->setFillStyle(ShapeStyle::Make(layerShader->makeWithMatrix(matrix)));
  list.root()->addChild(layer);

  auto layer2 = ShapeLayer::Make();
  layer2->setPath(path);
  auto imageShader = Shader::MakeImageShader(MakeImage("resources/apitest/imageReplacement.png"),
                                             TileMode::Decal, TileMode::Decal);
  layer2->setFillStyle(ShapeStyle::Make(imageShader->makeWithMatrix(matrix)));
  layer2->setMatrix(Matrix::MakeTrans(100, 0));
  list.root()->addChild(layer2);

  auto layer3 = ShapeLayer::Make();
  layer3->setPath(path);
  auto solidStyle = ShapeStyle::Make(Color::Red());
  layer3->setFillStyle(solidStyle);
  layer3->setMatrix(Matrix::MakeTrans(200, 0));
  list.root()->addChild(layer3);

  auto surface = Surface::Make(context, 300, 100);
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/ShapeStyleWithMatrix"));
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
  auto strokeStyle = ShapeStyle::Make(Color::FromRGBA(100, 0, 0));
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
  childLayer->setFillStyles({tgfx::ShapeStyle::Make(tgfx::Color::Red())});

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
  layer->setFillStyle(ShapeStyle::Make(Color::Red()));
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
  shapeLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 255, 255, 50)));
  shapeLayer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  rootLayer->addChild(shapeLayer);
  auto layerInvisible = SolidLayer::Make();
  layerInvisible->setColor(Color::FromRGBA(0, 0, 0, 255));
  layerInvisible->setWidth(100);
  layerInvisible->setHeight(100);
  layerInvisible->setMatrix(Matrix::MakeTrans(100, 100));
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
  canvas->restore();

  canvas->clear();
  canvas->rotate(30, 45, 45);
  canvas->clipRect(Rect::MakeXYWH(0, 0, 110, 110));
  canvas->scale(2.0f, 1.0f);
  canvas->translate(20, 20);
  shapeLayer->draw(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PartialDrawLayer_shapeLayer"));
}

TGFX_TEST(LayerTest, ContourTest) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  EXPECT_TRUE(image != nullptr);
  auto imageShader1 = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Repeat);
  auto imageShader2 = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp);
  auto imageStyle1 = ShapeStyle::Make(imageShader1);
  auto imageStyle2 = ShapeStyle::Make(imageShader2);

  DrawArgs drawArgs = DrawArgs(nullptr);
  Path path = {};

  // Case 1: Same geometry with all solid fills should dedup to 1 contour.
  auto allSolidLayer = ShapeLayer::Make();
  path.reset();
  path.addRect(Rect::MakeXYWH(10, 10, 80, 80));
  allSolidLayer->setPath(path);
  allSolidLayer->addFillStyle(ShapeStyle::Make(Color::Red()));
  allSolidLayer->addFillStyle(ShapeStyle::Make(Color::Blue()));
  ContourContext allSolidContext;
  auto allSolidCanvas = allSolidContext.beginRecording();
  allSolidLayer->drawContour(drawArgs, allSolidCanvas, 1.0f, BlendMode::SrcOver);
  auto allSolidPicture = allSolidContext.finishRecordingAsPicture();
  EXPECT_EQ(allSolidPicture->drawCount, 1u);

  // Case 2: Same geometry with all image shader fills should collect all (no dedup).
  auto allImageLayer = ShapeLayer::Make();
  path.reset();
  path.addRect(Rect::MakeXYWH(110, 10, 80, 80));
  allImageLayer->setPath(path);
  allImageLayer->addFillStyle(imageStyle1);
  allImageLayer->addFillStyle(imageStyle2);
  ContourContext allImageContext;
  auto allImageCanvas = allImageContext.beginRecording();
  allImageLayer->drawContour(drawArgs, allImageCanvas, 1.0f, BlendMode::SrcOver);
  auto allImagePicture = allImageContext.finishRecordingAsPicture();
  EXPECT_EQ(allImagePicture->drawCount, 2u);

  // Case 3: Same geometry with image-image-solid order should dedup to 1 contour.
  // This exposes the bug where single-pass logic collects both image shaders first,
  // then encounters solid and realizes group has non-image-shader, but already collected 2.
  auto mixedFillsLayer = ShapeLayer::Make();
  path.reset();
  path.addRect(Rect::MakeXYWH(10, 110, 80, 80));
  mixedFillsLayer->setPath(path);
  mixedFillsLayer->addFillStyle(imageStyle1);
  mixedFillsLayer->addFillStyle(imageStyle2);
  mixedFillsLayer->addFillStyle(ShapeStyle::Make(Color::Green()));
  ContourContext mixedFillsContext;
  auto mixedFillsCanvas = mixedFillsContext.beginRecording();
  mixedFillsLayer->drawContour(drawArgs, mixedFillsCanvas, 1.0f, BlendMode::SrcOver);
  auto mixedFillsPicture = mixedFillsContext.finishRecordingAsPicture();
  // Should be 1 (only first collected). Buggy single-pass would return 2.
  EXPECT_EQ(mixedFillsPicture->drawCount, 1u);

  // Case 4: Multiple groups with different dedup requirements.
  // Group 1 (image-image): collect all (2). Group 2 (solid-solid): dedup to 1.
  auto twoGroupsLayer = ShapeLayer::Make();
  path.reset();
  path.addRect(Rect::MakeXYWH(110, 110, 80, 80));
  twoGroupsLayer->setPath(path);
  twoGroupsLayer->addFillStyle(imageStyle1);
  twoGroupsLayer->addFillStyle(imageStyle2);
  auto childLayer = ShapeLayer::Make();
  path.reset();
  path.addRRect(RRect{Rect::MakeXYWH(120, 120, 60, 60), Point::Make(10, 10)});
  childLayer->setPath(path);
  childLayer->addFillStyle(ShapeStyle::Make(Color::Red()));
  childLayer->addFillStyle(ShapeStyle::Make(Color::Blue()));
  twoGroupsLayer->addChild(childLayer);
  ContourContext twoGroupsContext;
  auto twoGroupsCanvas = twoGroupsContext.beginRecording();
  twoGroupsLayer->drawContour(drawArgs, twoGroupsCanvas, 1.0f, BlendMode::SrcOver);
  auto twoGroupsPicture = twoGroupsContext.finishRecordingAsPicture();
  // Group 1: 2 (all image shaders), Group 2: 1 (deduped) = 3 total.
  EXPECT_EQ(twoGroupsPicture->drawCount, 3u);

  // Case 5: Stroke contour test - same geometry with same stroke should dedup.
  // ShapeLayer without fillStyle adds a transparent fill content (no stroke),
  // so we have 1 fill contour + 1 stroke contour (deduped from 2 strokes) = 2 total.
  auto strokeTestLayer = ShapeLayer::Make();
  path.reset();
  path.addRect(Rect::MakeXYWH(10, 210, 80, 80));
  strokeTestLayer->setPath(path);
  strokeTestLayer->setLineWidth(5.0f);
  strokeTestLayer->addStrokeStyle(ShapeStyle::Make(Color::Red()));
  strokeTestLayer->addStrokeStyle(ShapeStyle::Make(Color::Blue()));
  ContourContext strokeTestContext;
  auto strokeTestCanvas = strokeTestContext.beginRecording();
  strokeTestLayer->drawContour(drawArgs, strokeTestCanvas, 1.0f, BlendMode::SrcOver);
  auto strokeTestPicture = strokeTestContext.finishRecordingAsPicture();
  // 1 (transparent fill) + 1 (strokes deduped) = 2 contours.
  EXPECT_EQ(strokeTestPicture->drawCount, 2u);

  // Draw all to canvas for baseline comparison.
  auto rootLayer = ShapeLayer::Make();
  rootLayer->addChild(allSolidLayer);
  rootLayer->addChild(allImageLayer);
  rootLayer->addChild(mixedFillsLayer);
  rootLayer->addChild(twoGroupsLayer);
  rootLayer->addChild(strokeTestLayer);
  ContourContext allContext;
  auto allCanvas = allContext.beginRecording();
  rootLayer->drawContour(drawArgs, allCanvas, 1.0f, BlendMode::SrcOver);
  auto allPicture = allContext.finishRecordingAsPicture();
  // 1 + 2 + 1 + 3 + 2 = 9
  EXPECT_EQ(allPicture->drawCount, 9u);

  surface = Surface::Make(context, 200, 300);
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->drawPicture(allPicture);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/ContourTest"));
}

TGFX_TEST(LayerTest, ContourMatchesContent) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  DrawArgs drawArgs = DrawArgs(nullptr);
  Path path = {};
  path.addRect(Rect::MakeXYWH(10, 10, 80, 80));

  // Case 1: Opaque solid fill should match content.
  auto opaqueLayer = ShapeLayer::Make();
  opaqueLayer->setPath(path);
  opaqueLayer->addFillStyle(ShapeStyle::Make(Color::Red()));
  ContourContext opaqueContext;
  auto opaqueCanvas = opaqueContext.beginRecording();
  bool opaqueMatch = opaqueLayer->drawContour(drawArgs, opaqueCanvas, 1.0f, BlendMode::SrcOver);
  opaqueContext.finishRecordingAsPicture();
  EXPECT_TRUE(opaqueMatch);

  // Case 2: Transparent fill (alpha = 0) should not match content.
  auto transparentLayer = ShapeLayer::Make();
  transparentLayer->setPath(path);
  transparentLayer->addFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 0, 0, 0)));
  ContourContext transparentContext;
  auto transparentCanvas = transparentContext.beginRecording();
  bool transparentMatch =
      transparentLayer->drawContour(drawArgs, transparentCanvas, 1.0f, BlendMode::SrcOver);
  transparentContext.finishRecordingAsPicture();
  EXPECT_FALSE(transparentMatch);

  // Case 3: Image shader fill should match content.
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  auto imageShader = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Repeat);
  auto imageLayer = ShapeLayer::Make();
  imageLayer->setPath(path);
  imageLayer->addFillStyle(ShapeStyle::Make(imageShader));
  ContourContext imageContext;
  auto imageCanvas = imageContext.beginRecording();
  bool imageMatch = imageLayer->drawContour(drawArgs, imageCanvas, 1.0f, BlendMode::SrcOver);
  imageContext.finishRecordingAsPicture();
  EXPECT_TRUE(imageMatch);

  // Case 4: Layer with filter should not match content (child layer check).
  auto parentLayer = Layer::Make();
  auto filterLayer = ShapeLayer::Make();
  filterLayer->setPath(path);
  filterLayer->addFillStyle(ShapeStyle::Make(Color::Red()));
  filterLayer->setFilters({BlurFilter::Make(5, 5)});
  parentLayer->addChild(filterLayer);
  ContourContext filterContext;
  auto filterCanvas = filterContext.beginRecording();
  bool filterMatch = parentLayer->drawContour(drawArgs, filterCanvas, 1.0f, BlendMode::SrcOver);
  filterContext.finishRecordingAsPicture();
  EXPECT_FALSE(filterMatch);

  // Case 5: Layer with layerStyle should not match content (child layer check).
  auto parentLayer2 = Layer::Make();
  auto styleLayer = ShapeLayer::Make();
  styleLayer->setPath(path);
  styleLayer->addFillStyle(ShapeStyle::Make(Color::Red()));
  styleLayer->setLayerStyles({DropShadowStyle::Make(5, 5, 0, 5, Color::Black())});
  parentLayer2->addChild(styleLayer);
  ContourContext styleContext;
  auto styleCanvas = styleContext.beginRecording();
  bool styleMatch = parentLayer2->drawContour(drawArgs, styleCanvas, 1.0f, BlendMode::SrcOver);
  styleContext.finishRecordingAsPicture();
  EXPECT_FALSE(styleMatch);

  // Case 6: Gradient shader should check isOpaque().
  auto gradientShader = Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(100, 100),
                                                   {Color::Red(), Color::Blue()}, {0.0f, 1.0f});
  auto gradientLayer = ShapeLayer::Make();
  gradientLayer->setPath(path);
  gradientLayer->addFillStyle(ShapeStyle::Make(gradientShader));
  ContourContext gradientContext;
  auto gradientCanvas = gradientContext.beginRecording();
  bool gradientMatch =
      gradientLayer->drawContour(drawArgs, gradientCanvas, 1.0f, BlendMode::SrcOver);
  gradientContext.finishRecordingAsPicture();
  // Gradient with opaque colors should match.
  EXPECT_TRUE(gradientMatch);
}

TGFX_TEST(LayerTest, ContourContainsOpaqueBounds) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  DrawArgs drawArgs = DrawArgs(nullptr);
  Path path = {};

  // Create a parent layer with opaque fill covering children.
  auto parentLayer = ShapeLayer::Make();
  path.addRect(Rect::MakeXYWH(0, 0, 200, 200));
  parentLayer->setPath(path);
  parentLayer->addFillStyle(ShapeStyle::Make(Color::Red()));

  // Child layer completely covered by parent's opaque bounds should be skipped.
  auto childLayer = ShapeLayer::Make();
  path.reset();
  path.addRect(Rect::MakeXYWH(50, 50, 100, 100));
  childLayer->setPath(path);
  childLayer->addFillStyle(ShapeStyle::Make(Color::Blue()));
  parentLayer->addChild(childLayer);

  ContourContext contourContext;
  auto canvas = contourContext.beginRecording();
  parentLayer->drawContour(drawArgs, canvas, 1.0f, BlendMode::SrcOver);
  auto picture = contourContext.finishRecordingAsPicture();
  // Parent contour drawn, child skipped due to containsOpaqueBounds optimization.
  // Only 1 draw call for parent.
  EXPECT_EQ(picture->drawCount, 1u);

  // Now test with child not covered (partially outside).
  auto parentLayer2 = ShapeLayer::Make();
  path.reset();
  path.addRect(Rect::MakeXYWH(0, 0, 100, 100));
  parentLayer2->setPath(path);
  parentLayer2->addFillStyle(ShapeStyle::Make(Color::Red()));

  auto childLayer2 = ShapeLayer::Make();
  path.reset();
  path.addRect(Rect::MakeXYWH(50, 50, 100, 100));
  childLayer2->setPath(path);
  childLayer2->addFillStyle(ShapeStyle::Make(Color::Blue()));
  parentLayer2->addChild(childLayer2);

  ContourContext contourContext2;
  auto canvas2 = contourContext2.beginRecording();
  parentLayer2->drawContour(drawArgs, canvas2, 1.0f, BlendMode::SrcOver);
  auto picture2 = contourContext2.finishRecordingAsPicture();
  // Both parent and child contours should be drawn.
  EXPECT_EQ(picture2->drawCount, 2u);
}

TGFX_TEST(LayerTest, GetContourImage) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);

  // Create a shape layer with a rectangle.
  auto shapeLayer = ShapeLayer::Make();
  Path path = {};
  path.addRect(Rect::MakeXYWH(10, 10, 80, 80));
  shapeLayer->setPath(path);
  shapeLayer->addFillStyle(ShapeStyle::Make(Color::Red()));

  // Get contour image.
  DrawArgs drawArgs = DrawArgs(context);
  Point offset = {};
  bool contourMatchesContent = false;
  auto contourImage =
      shapeLayer->getContentContourImage(drawArgs, 1.0f, &offset, &contourMatchesContent);
  EXPECT_TRUE(contourImage != nullptr);
  EXPECT_EQ(offset.x, 10.0f);
  EXPECT_EQ(offset.y, 10.0f);
  EXPECT_EQ(contourImage->width(), 80);
  EXPECT_EQ(contourImage->height(), 80);
  EXPECT_TRUE(contourMatchesContent);

  // Draw the contour image to verify.
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->drawImage(contourImage, offset.x, offset.y);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/GetContourImage"));
}

TGFX_TEST(LayerTest, ContourWithMask) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);

  DrawArgs drawArgs = DrawArgs(nullptr);
  Path path = {};

  // Create a shape layer with mask.
  auto shapeLayer = ShapeLayer::Make();
  path.addRect(Rect::MakeXYWH(10, 10, 100, 100));
  shapeLayer->setPath(path);
  shapeLayer->addFillStyle(ShapeStyle::Make(Color::Red()));

  // Create a mask layer (circle).
  auto maskLayer = ShapeLayer::Make();
  path.reset();
  path.addOval(Rect::MakeXYWH(30, 30, 60, 60));
  maskLayer->setPath(path);
  maskLayer->addFillStyle(ShapeStyle::Make(Color::White()));
  shapeLayer->setMask(maskLayer);
  shapeLayer->setMaskType(LayerMaskType::Contour);

  ContourContext contourContext;
  auto canvas = contourContext.beginRecording();
  shapeLayer->drawContour(drawArgs, canvas, 1.0f, BlendMode::SrcOver);
  auto picture = contourContext.finishRecordingAsPicture();
  // The mask clips the shape, so we should have 1 contour (the masked shape).
  EXPECT_EQ(picture->drawCount, 1u);

  // Draw to surface for visual verification.
  surface = Surface::Make(context, 200, 200);
  auto drawCanvas = surface->getCanvas();
  drawCanvas->clear();
  drawCanvas->drawPicture(picture);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/ContourWithMask"));
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
  auto imageShader =
      Shader::MakeImageShader(image, TileMode::Decal, TileMode::Decal,
                              SamplingOptions(FilterMode::Linear, FilterMode::Nearest));
  DisplayList displayList;
  displayList.root()->addChild(shapeLayer);
  shapeLayer->setFillStyle(ShapeStyle::Make(imageShader));
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
  auto imageShader =
      Shader::MakeImageShader(image, TileMode::Decal, TileMode::Decal,
                              SamplingOptions(FilterMode::Linear, FilterMode::Nearest));
  auto filter = DropShadowFilter::Make(-10, -10, 5, 5, Color::Black());
  DisplayList displayList;
  displayList.root()->addChild(shapeLayer);
  shapeLayer->setFillStyle(ShapeStyle::Make(imageShader));
  shapeLayer->setFilters({filter});

  auto glassLayer = ShapeLayer::Make();
  Path glassPath;
  glassPath.addRRect(RRect{Rect::MakeXYWH(10, 10, 80, 80), Point::Make(40, 40)});
  glassLayer->setPath(glassPath);
  glassLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 255, 255, 50)));
  glassLayer->setLayerStyles({BackgroundBlurStyle::Make(5, 5)});
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
  auto imageShader =
      Shader::MakeImageShader(image, TileMode::Decal, TileMode::Decal,
                              SamplingOptions(FilterMode::Linear, FilterMode::Nearest));
  shapeLayer->setFillStyle(
      ShapeStyle::Make(imageShader->makeWithMatrix(Matrix::MakeTrans(-40, -40))));
  shapeLayer->setMatrix(Matrix::MakeRotate(45, 30, 30));

  auto childLayer = ShapeLayer::Make();
  Path childPath;
  childPath.addRect(Rect::MakeXYWH(25, 25, 50, 50));
  childLayer->setPath(childPath);
  childLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(0, 0, 255, 255)));
  childLayer->setAlpha(0.5f);
  shapeLayer->addChild(childLayer);

  auto childLayer2 = ShapeLayer::Make();
  Path childPath2;
  childPath2.addRect(Rect::MakeXYWH(10, 10, 30, 30));
  childLayer2->setPath(childPath2);
  childLayer2->setFillStyle(ShapeStyle::Make(Color::FromRGBA(0, 255, 0, 255)));
  childLayer2->setBlendMode(BlendMode::Exclusion);
  childLayer->addChild(childLayer2);

  DisplayList displayList;
  displayList.root()->addChild(shapeLayer);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PassThrough_Test"));
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
  displayList->setSubtreeCacheMaxSize(500);

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
    modelMatrix.postTranslate(0.f, 0.f, 20.f);
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
  auto imageToContentBounds = imageLayer->getBounds(contentLayer.get());
  imageToContentBounds.roundOut();
  EXPECT_EQ(imageToContentBounds, Rect::MakeLTRB(73, 10, 290, 272));
  auto imageToDisplayListBounds = imageLayer->getBounds(displayList->root());
  imageToDisplayListBounds.roundOut();
  EXPECT_EQ(imageToDisplayListBounds, Rect::MakeLTRB(102, 21, 187, 158));
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/Matrix_3D"));

  auto imageBlurLayer = SolidLayer::Make();
  imageBlurLayer->setColor(Color::FromRGBA(235, 5, 112, 70));
  imageBlurLayer->setWidth(170);
  imageBlurLayer->setHeight(70);
  imageBlurLayer->setMatrix(Matrix::MakeTrans(-30.f, 20.f));
  imageBlurLayer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  imageLayer->addChild(imageBlurLayer);
  auto affineMatrix = Matrix::MakeTrans(50, 50);
  imageLayer->setMatrix(affineMatrix);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/Matrix_3D_2D"));

  imageBlurLayer->removeFromParent();
  imageLayer->setMatrix3D(imageMatrix3D);
  EXPECT_TRUE(imageLayer->matrix().isIdentity());
  auto rect = Rect::MakeXYWH(50, 50, 200, 100);
  Path path = {};
  path.addRoundRect(rect, 20, 20);
  auto shaperLayer = ShapeLayer::Make();
  shaperLayer->setPath(path);
  shaperLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(0, 0, 255, 128)));
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

  contentLayer->setPreserve3D(true);
  imageToContentBounds = imageLayer->getBounds(contentLayer.get());
  imageToContentBounds.roundOut();
  EXPECT_EQ(imageToContentBounds, Rect::MakeLTRB(-51, 10, 333, 279));
  imageToDisplayListBounds = imageLayer->getBounds(displayList->root());
  imageToDisplayListBounds.roundOut();
  EXPECT_EQ(imageToDisplayListBounds, Rect::MakeLTRB(62, 21, 206, 159));
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/Matrix_3D_2D_3D_Preserve3D"));

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

TGFX_TEST(LayerTest, DisplayListBackground) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto displayList = std::make_unique<DisplayList>();

  // Test default background color (transparent)
  auto defaultColor = displayList->backgroundColor();
  EXPECT_EQ(defaultColor, Color::Transparent());

  // Test setting opaque background color
  auto opaqueColor = Color::FromRGBA(255, 0, 0, 255);
  displayList->setBackgroundColor(opaqueColor);
  EXPECT_EQ(displayList->backgroundColor(), opaqueColor);

  // Render with opaque background
  auto rootLayer = Layer::Make();
  displayList->root()->addChild(rootLayer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DisplayListBackground_OpaqueRed"));

  // Test setting semi-transparent background color
  auto semiTransparentColor = Color::FromRGBA(0, 255, 0, 128);
  displayList->setBackgroundColor(semiTransparentColor);
  EXPECT_EQ(displayList->backgroundColor(), semiTransparentColor);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DisplayListBackground_SemiTransparentGreen"));

  // Test setting different background colors
  auto blueColor = Color::FromRGBA(0, 0, 255, 255);
  displayList->setBackgroundColor(blueColor);
  EXPECT_EQ(displayList->backgroundColor(), blueColor);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DisplayListBackground_Blue"));

  // Test adding layers and rendering with different background colors
  auto shapeLayer = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeXYWH(50, 50, 100, 100));
  shapeLayer->setPath(path);
  shapeLayer->setFillStyle(ShapeStyle::Make(Color::White()));
  rootLayer->addChild(shapeLayer);

  auto whiteBackgroundColor = Color::FromRGBA(255, 255, 255, 255);
  displayList->setBackgroundColor(whiteBackgroundColor);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DisplayListBackground_WhiteWithShape"));

  // Test resetting to transparent background
  displayList->setBackgroundColor(Color::Transparent());
  EXPECT_EQ(displayList->backgroundColor(), Color::Transparent());
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DisplayListBackground_TransparentWithShape"));

  // Test background color with different render modes
  displayList->setBackgroundColor(Color::FromRGBA(200, 100, 50, 255));
  displayList->setRenderMode(RenderMode::Partial);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DisplayListBackground_PartialRender"));

  displayList->setRenderMode(RenderMode::Tiled);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DisplayListBackground_TiledRender"));
}

TGFX_TEST(LayerTest, LayerRecorder) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  LayerPaint redPaint = {};
  redPaint.color = Color::Red();

  LayerPaint bluePaint = {};
  bluePaint.color = Color::Blue();

  LayerPaint strokePaint = {};
  strokePaint.color = Color::Red();
  strokePaint.style = PaintStyle::Stroke;
  strokePaint.stroke.width = 5.0f;

  // Test 1: Multiple rects with same paint should merge into RectsContent
  {
    auto surface = Surface::Make(context, 200, 150);
    LayerRecorder recorder = {};
    recorder.addRect(Rect::MakeXYWH(10, 10, 50, 50), redPaint);
    recorder.addRect(Rect::MakeXYWH(70, 10, 50, 50), redPaint);
    recorder.addRect(Rect::MakeXYWH(130, 10, 50, 50), redPaint);
    recorder.addRect(Rect::MakeXYWH(10, 70, 50, 50), bluePaint);
    auto content = recorder.finishRecording();
    ASSERT_TRUE(content != nullptr);
    // Should be ComposeContent with 2 items: RectsContent (3 rects) + RectContent (1 rect)
    EXPECT_EQ(content->type(), LayerContent::Type::Compose);
    auto composeContent = static_cast<ComposeContent*>(content.get());
    EXPECT_EQ(composeContent->contents.size(), 2u);
    EXPECT_EQ(composeContent->contents[0]->type(), LayerContent::Type::Rects);
    auto rectsContent = static_cast<RectsContent*>(composeContent->contents[0].get());
    EXPECT_EQ(rectsContent->rects.size(), 3u);
    EXPECT_EQ(composeContent->contents[1]->type(), LayerContent::Type::Rect);
    content->drawDefault(surface->getCanvas(), 1.0f, true);
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerRecorder_MultipleRects"));
  }

  // Test 2: Multiple rrects with same paint should merge into RRectsContent
  {
    auto surface = Surface::Make(context, 200, 150);
    LayerRecorder recorder = {};
    RRect rRect1 = {};
    rRect1.setRectXY(Rect::MakeXYWH(10, 10, 50, 50), 10, 10);
    RRect rRect2 = {};
    rRect2.setRectXY(Rect::MakeXYWH(70, 10, 50, 50), 10, 10);
    RRect rRect3 = {};
    rRect3.setRectXY(Rect::MakeXYWH(130, 10, 50, 50), 10, 10);
    RRect rRect4 = {};
    rRect4.setRectXY(Rect::MakeXYWH(10, 70, 50, 50), 10, 10);
    recorder.addRRect(rRect1, redPaint);
    recorder.addRRect(rRect2, redPaint);
    recorder.addRRect(rRect3, redPaint);
    recorder.addRRect(rRect4, bluePaint);
    auto content = recorder.finishRecording();
    ASSERT_TRUE(content != nullptr);
    // Should be ComposeContent with 2 items: RRectsContent (3 rrects) + RRectContent (1 rrect)
    EXPECT_EQ(content->type(), LayerContent::Type::Compose);
    auto composeContent = static_cast<ComposeContent*>(content.get());
    EXPECT_EQ(composeContent->contents.size(), 2u);
    EXPECT_EQ(composeContent->contents[0]->type(), LayerContent::Type::RRects);
    auto rrectsContent = static_cast<RRectsContent*>(composeContent->contents[0].get());
    EXPECT_EQ(rrectsContent->rRects.size(), 3u);
    EXPECT_EQ(composeContent->contents[1]->type(), LayerContent::Type::RRect);
    content->drawDefault(surface->getCanvas(), 1.0f, true);
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerRecorder_MultipleRRects"));
  }

  // Test 3: Multiple paths with same paint should merge into single Shape
  {
    auto surface = Surface::Make(context, 200, 150);
    LayerRecorder recorder = {};
    Path path1 = {};
    path1.addOval(Rect::MakeXYWH(10, 10, 50, 50));
    Path path2 = {};
    path2.addOval(Rect::MakeXYWH(70, 10, 50, 50));
    Path path3 = {};
    path3.addOval(Rect::MakeXYWH(130, 10, 50, 50));
    Path path4 = {};
    path4.addOval(Rect::MakeXYWH(10, 70, 50, 50));
    recorder.addPath(path1, redPaint);
    recorder.addPath(path2, redPaint);
    recorder.addPath(path3, redPaint);
    recorder.addPath(path4, bluePaint);
    auto content = recorder.finishRecording();
    ASSERT_TRUE(content != nullptr);
    // Should be ComposeContent with 2 items: ShapeContent (merged paths) + PathContent
    EXPECT_EQ(content->type(), LayerContent::Type::Compose);
    auto composeContent = static_cast<ComposeContent*>(content.get());
    ASSERT_TRUE(composeContent->contents.size() == 4u);
    EXPECT_EQ(composeContent->contents[0]->type(), LayerContent::Type::Path);
    EXPECT_EQ(composeContent->contents[1]->type(), LayerContent::Type::Path);
    EXPECT_EQ(composeContent->contents[2]->type(), LayerContent::Type::Path);
    EXPECT_EQ(composeContent->contents[3]->type(), LayerContent::Type::Path);
    content->drawDefault(surface->getCanvas(), 1.0f, true);
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerRecorder_MultiplePaths"));
  }

  // Test 4: Mixed shapes with same paint should create separate contents
  {
    auto surface = Surface::Make(context, 200, 100);
    LayerRecorder recorder = {};
    recorder.addRect(Rect::MakeXYWH(10, 10, 50, 50), redPaint);
    RRect rRect = {};
    rRect.setRectXY(Rect::MakeXYWH(70, 10, 50, 50), 10, 10);
    recorder.addRRect(rRect, redPaint);
    Path path = {};
    path.addOval(Rect::MakeXYWH(130, 10, 50, 50));
    recorder.addPath(path, redPaint);
    auto content = recorder.finishRecording();
    ASSERT_TRUE(content != nullptr);
    // Should be ComposeContent with 3 items: RectContent + RRectContent + PathContent
    EXPECT_EQ(content->type(), LayerContent::Type::Compose);
    auto composeContent = static_cast<ComposeContent*>(content.get());
    EXPECT_EQ(composeContent->contents.size(), 3u);
    EXPECT_EQ(composeContent->contents[0]->type(), LayerContent::Type::Rect);
    EXPECT_EQ(composeContent->contents[1]->type(), LayerContent::Type::RRect);
    EXPECT_EQ(composeContent->contents[2]->type(), LayerContent::Type::Path);
    content->drawDefault(surface->getCanvas(), 1.0f, true);
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerRecorder_MixedShapes"));
  }

  // Test 5: TextBlob with x, y offset
  {
    auto surface = Surface::Make(context, 200, 150);
    LayerRecorder recorder = {};
    auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
    ASSERT_TRUE(typeface != nullptr);
    auto font = Font(typeface, 24);
    auto textBlob = TextBlob::MakeFrom("Hello", font);
    recorder.addTextBlob(textBlob, redPaint, 50, 100);
    auto content = recorder.finishRecording();
    ASSERT_TRUE(content != nullptr);
    // Should be single TextContent
    EXPECT_EQ(content->type(), LayerContent::Type::Text);
    auto textContent = static_cast<TextContent*>(content.get());
    EXPECT_EQ(textContent->textMatrix, Matrix::MakeTrans(50, 100));
    content->drawDefault(surface->getCanvas(), 1.0f, true);
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerRecorder_TextBlobWithOffset"));
  }

  // Test 6: Line path with fill should be ignored
  {
    auto surface = Surface::Make(context, 200, 150);
    LayerRecorder recorder = {};
    Path linePath = {};
    linePath.moveTo(10, 10);
    linePath.lineTo(100, 100);
    recorder.addPath(linePath, redPaint);  // Fill style, should be ignored
    recorder.addRect(Rect::MakeXYWH(10, 70, 50, 50), bluePaint);
    auto content = recorder.finishRecording();
    ASSERT_TRUE(content != nullptr);
    // Should be single RectContent (line path ignored)
    EXPECT_EQ(content->type(), LayerContent::Type::Rect);
    content->drawDefault(surface->getCanvas(), 1.0f, true);
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerRecorder_LinePathFill"));
  }

  // Test 7: Line path with stroke should be converted to rect
  {
    auto surface = Surface::Make(context, 200, 100);
    LayerRecorder recorder = {};
    Path linePath = {};
    linePath.moveTo(10, 50);
    linePath.lineTo(190, 50);
    recorder.addPath(linePath, strokePaint);
    auto content = recorder.finishRecording();
    ASSERT_TRUE(content != nullptr);
    // Should be single RectContent (line converted to rect)
    EXPECT_EQ(content->type(), LayerContent::Type::Rect);
    content->drawDefault(surface->getCanvas(), 1.0f, true);
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerRecorder_LinePathStroke"));
  }

  // Test 8: Single rect should be RectContent, not RectsContent
  {
    LayerRecorder recorder = {};
    recorder.addRect(Rect::MakeXYWH(10, 10, 50, 50), redPaint);
    auto content = recorder.finishRecording();
    ASSERT_TRUE(content != nullptr);
    EXPECT_EQ(content->type(), LayerContent::Type::Rect);
  }

  // Test 9: Single rrect should be RRectContent, not RRectsContent
  {
    LayerRecorder recorder = {};
    RRect rRect = {};
    rRect.setRectXY(Rect::MakeXYWH(10, 10, 50, 50), 10, 10);
    recorder.addRRect(rRect, redPaint);
    auto content = recorder.finishRecording();
    ASSERT_TRUE(content != nullptr);
    EXPECT_EQ(content->type(), LayerContent::Type::RRect);
  }

  // Test 10: RRect with zero radius should be converted to Rect
  {
    LayerRecorder recorder = {};
    RRect rRect = {};
    rRect.setRectXY(Rect::MakeXYWH(10, 10, 50, 50), 0, 0);
    recorder.addRRect(rRect, redPaint);
    auto content = recorder.finishRecording();
    ASSERT_TRUE(content != nullptr);
    EXPECT_EQ(content->type(), LayerContent::Type::Rect);
  }
}

TGFX_TEST(LayerTest, GetRotateBounds) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();
  displayList->root()->addChild(rootLayer);

  auto outerLayer = ShapeLayer::Make();
  Path outerPath = {};
  outerPath.addRect(Rect::MakeWH(300, 300));
  outerLayer->setPath(outerPath);
  outerLayer->setFillStyle(ShapeStyle::Make(Color::Blue()));
  outerLayer->setMatrix(Matrix::MakeTrans(200, 200));
  rootLayer->addChild(outerLayer);

  auto middleLayer = ShapeLayer::Make();
  Path middlePath = {};
  middlePath.addRect(Rect::MakeWH(200, 200));
  middleLayer->setPath(middlePath);
  middleLayer->setFillStyle(ShapeStyle::Make(Color::Red()));
  middleLayer->setMatrix(Matrix::MakeTrans(50, 50) * Matrix::MakeRotate(45, 100, 100));
  outerLayer->addChild(middleLayer);

  auto style = DropShadowStyle::Make(-100, 0, 0, 0, Color::Black(), true);
  middleLayer->setLayerStyles({style});

  auto bounds = middleLayer->getBounds(outerLayer.get(), true);
  EXPECT_FLOAT_EQ(bounds.left, -62.132034f);
  EXPECT_FLOAT_EQ(bounds.top, -62.132034f);
  EXPECT_FLOAT_EQ(bounds.right, 291.42136f);
  EXPECT_FLOAT_EQ(bounds.bottom, 291.42136f);
}

TGFX_TEST(LayerTest, SetChildrenOptimization) {
  // Test the LIS algorithm optimization in setChildren method
  // This test verifies that the dirty marking optimization works correctly
  // for various layer reordering scenarios

  auto parent = Layer::Make();

  // Create test layers with names for easier tracking
  auto layerA = Layer::Make();
  layerA->setName("A");
  auto layerB = Layer::Make();
  layerB->setName("B");
  auto layerC = Layer::Make();
  layerC->setName("C");
  auto layerD = Layer::Make();
  layerD->setName("D");
  auto layerE = Layer::Make();
  layerE->setName("E");

  // Test Case 1: Simple reordering - should preserve some layers
  std::vector<std::shared_ptr<Layer>> initialOrder = {layerA, layerB, layerC, layerD, layerE};
  parent->setChildren(initialOrder);

  // Verify initial setup
  EXPECT_EQ(parent->children().size(), 5u);
  EXPECT_EQ(parent->children()[0], layerA);
  EXPECT_EQ(parent->children()[1], layerB);
  EXPECT_EQ(parent->children()[2], layerC);
  EXPECT_EQ(parent->children()[3], layerD);
  EXPECT_EQ(parent->children()[4], layerE);

  // Test Case 2: Reverse order - LIS optimization should minimize dirty layers
  std::vector<std::shared_ptr<Layer>> reverseOrder = {layerE, layerD, layerC, layerB, layerA};
  parent->setChildren(reverseOrder);

  // Verify new order
  EXPECT_EQ(parent->children().size(), 5u);
  EXPECT_EQ(parent->children()[0], layerE);
  EXPECT_EQ(parent->children()[1], layerD);
  EXPECT_EQ(parent->children()[2], layerC);
  EXPECT_EQ(parent->children()[3], layerB);
  EXPECT_EQ(parent->children()[4], layerA);

  // Test Case 3: Partial reordering - some layers maintain relative order
  std::vector<std::shared_ptr<Layer>> partialReorder = {layerB, layerD, layerA, layerE, layerC};
  parent->setChildren(partialReorder);

  // Verify partial reorder
  EXPECT_EQ(parent->children().size(), 5u);
  EXPECT_EQ(parent->children()[0], layerB);
  EXPECT_EQ(parent->children()[1], layerD);
  EXPECT_EQ(parent->children()[2], layerA);
  EXPECT_EQ(parent->children()[3], layerE);
  EXPECT_EQ(parent->children()[4], layerC);

  // Test Case 4: Single element move - minimal dirty area expected
  std::vector<std::shared_ptr<Layer>> singleMove = {layerA, layerC, layerD, layerE, layerB};
  parent->setChildren(singleMove);

  // Verify single move (B moved from position 0 to position 4)
  EXPECT_EQ(parent->children().size(), 5u);
  EXPECT_EQ(parent->children()[0], layerA);
  EXPECT_EQ(parent->children()[1], layerC);
  EXPECT_EQ(parent->children()[2], layerD);
  EXPECT_EQ(parent->children()[3], layerE);
  EXPECT_EQ(parent->children()[4], layerB);

  // Test Case 5: Add and remove layers simultaneously
  auto layerF = Layer::Make();
  layerF->setName("F");
  auto layerG = Layer::Make();
  layerG->setName("G");

  std::vector<std::shared_ptr<Layer>> addRemove = {layerF, layerB, layerG, layerC};
  parent->setChildren(addRemove);

  // Verify add/remove operation
  EXPECT_EQ(parent->children().size(), 4u);
  EXPECT_EQ(parent->children()[0], layerF);
  EXPECT_EQ(parent->children()[1], layerB);
  EXPECT_EQ(parent->children()[2], layerG);
  EXPECT_EQ(parent->children()[3], layerC);

  // Verify removed layers are properly detached
  EXPECT_EQ(layerA->parent(), nullptr);
  EXPECT_EQ(layerD->parent(), nullptr);
  EXPECT_EQ(layerE->parent(), nullptr);

  // Verify new layers are properly attached
  EXPECT_EQ(layerF->parent(), parent.get());
  EXPECT_EQ(layerG->parent(), parent.get());

  // Test Case 6: Empty to non-empty
  auto emptyParent = Layer::Make();
  std::vector<std::shared_ptr<Layer>> fromEmpty = {layerA, layerB};
  emptyParent->setChildren(fromEmpty);

  EXPECT_EQ(emptyParent->children().size(), 2u);
  EXPECT_EQ(layerA->parent(), emptyParent.get());
  EXPECT_EQ(layerB->parent(), emptyParent.get());

  // Test Case 7: Non-empty to empty
  std::vector<std::shared_ptr<Layer>> toEmpty = {};
  emptyParent->setChildren(toEmpty);

  EXPECT_EQ(emptyParent->children().size(), 0u);
  EXPECT_EQ(layerA->parent(), nullptr);
  EXPECT_EQ(layerB->parent(), nullptr);

  // Test Case 8: Duplicate layers (should be handled gracefully)
  std::vector<std::shared_ptr<Layer>> withDuplicates = {layerA, layerB, layerA, layerC};
  parent->setChildren(withDuplicates);

  // Should only add each layer once, keeping first occurrence
  EXPECT_EQ(parent->children().size(), 3u);
  EXPECT_EQ(parent->children()[0], layerA);
  EXPECT_EQ(parent->children()[1], layerB);
  EXPECT_EQ(parent->children()[2], layerC);

  // Test Case 9: Circular reference prevention
  auto childLayer = Layer::Make();
  auto grandChildLayer = Layer::Make();

  parent->addChild(childLayer);
  childLayer->addChild(grandChildLayer);

  // Try to add parent as child of grandchild (should be prevented)
  std::vector<std::shared_ptr<Layer>> circular = {parent};
  grandChildLayer->setChildren(circular);

  // The circular reference should be prevented, grandchild should remain empty
  EXPECT_EQ(grandChildLayer->children().size(), 0u);

  // Test Case 10: Root layer as child (should be prevented)
  auto rootLayer = RootLayer::Make();
  rootLayer->_root = rootLayer.get();

  std::vector<std::shared_ptr<Layer>> withRoot = {layerA, rootLayer, layerB};
  parent->setChildren(withRoot);

  // Root layer should be rejected, only layerA and layerB should be added
  EXPECT_EQ(parent->children().size(), 2u);
  EXPECT_EQ(parent->children()[0], layerA);
  EXPECT_EQ(parent->children()[1], layerB);
  EXPECT_FALSE(parent->contains(rootLayer));

  // Test Case 11: Self as child (should be prevented)
  std::vector<std::shared_ptr<Layer>> selfAsChild = {layerA, parent, layerB};
  parent->setChildren(selfAsChild);

  EXPECT_EQ(parent->children().size(), 2u);
  EXPECT_EQ(parent->children()[0], layerA);
  EXPECT_EQ(parent->children()[1], layerB);

  // Test Case 12: nullptr handling
  std::vector<std::shared_ptr<Layer>> withNullptr = {nullptr, layerA, nullptr, layerB, nullptr};
  parent->setChildren(withNullptr);

  EXPECT_EQ(parent->children().size(), 2u);
  EXPECT_EQ(parent->children()[0], layerA);
  EXPECT_EQ(parent->children()[1], layerB);

  // Test Case 13: Same content repeated call (should be no-op)
  std::vector<std::shared_ptr<Layer>> sameContent = {layerA, layerB};
  parent->setChildren(sameContent);
  auto childrenBefore = parent->children();
  parent->setChildren(sameContent);
  auto childrenAfter = parent->children();

  EXPECT_EQ(childrenBefore, childrenAfter);

  // Test Case 14: Steal child from another parent
  auto parentB = Layer::Make();
  auto layerX = Layer::Make();
  layerX->setName("X");
  parentB->addChild(layerX);

  EXPECT_EQ(layerX->parent(), parentB.get());

  std::vector<std::shared_ptr<Layer>> stealChild = {layerA, layerX, layerB};
  parent->setChildren(stealChild);

  EXPECT_EQ(layerX->parent(), parent.get());
  EXPECT_FALSE(parentB->contains(layerX));
  EXPECT_EQ(parent->children().size(), 3u);
  EXPECT_EQ(parent->children()[0], layerA);
  EXPECT_EQ(parent->children()[1], layerX);
  EXPECT_EQ(parent->children()[2], layerB);
}

TGFX_TEST(LayerTest, SetChildrenLISValidation) {
  // Test that the LIS algorithm correctly identifies which layers need to be marked dirty
  // by verifying that layers maintaining relative order are not unnecessarily invalidated

  auto displayList = std::make_unique<DisplayList>();
  auto root = static_cast<RootLayer*>(displayList->root());

  auto layerA = Layer::Make();
  layerA->setName("A");
  auto layerB = Layer::Make();
  layerB->setName("B");
  auto layerC = Layer::Make();
  layerC->setName("C");
  auto layerD = Layer::Make();
  layerD->setName("D");

  // Initial setup: [A, B, C, D]
  std::vector<std::shared_ptr<Layer>> initial = {layerA, layerB, layerC, layerD};
  root->setChildren(initial);
  root->updateDirtyRegions();

  // Scenario 1: Move D to front -> [D, A, B, C]
  // LIS of old positions in new order: A(0), B(1), C(2) maintain relative order
  // Only D should be marked dirty
  std::vector<std::shared_ptr<Layer>> moveDToFront = {layerD, layerA, layerB, layerC};
  root->setChildren(moveDToFront);

  EXPECT_EQ(root->children().size(), 4u);
  EXPECT_EQ(root->children()[0], layerD);
  EXPECT_EQ(root->children()[1], layerA);
  EXPECT_EQ(root->children()[2], layerB);
  EXPECT_EQ(root->children()[3], layerC);
  EXPECT_TRUE(layerD->bitFields.dirtyTransform);
  EXPECT_FALSE(layerA->bitFields.dirtyTransform);
  EXPECT_FALSE(layerB->bitFields.dirtyTransform);
  EXPECT_FALSE(layerC->bitFields.dirtyTransform);

  root->updateDirtyRegions();

  // Scenario 2: [D, A, B, C] -> [D, B, C, A]
  // Old positions in new order: D(0), B(2), C(3), A(1)
  // LIS: D(0), B(2), C(3) maintain relative order
  // Only A should be marked dirty
  std::vector<std::shared_ptr<Layer>> scenario2 = {layerD, layerB, layerC, layerA};
  root->setChildren(scenario2);

  EXPECT_EQ(root->children().size(), 4u);
  EXPECT_EQ(root->children()[0], layerD);
  EXPECT_EQ(root->children()[1], layerB);
  EXPECT_EQ(root->children()[2], layerC);
  EXPECT_EQ(root->children()[3], layerA);
  EXPECT_FALSE(layerD->bitFields.dirtyTransform);
  EXPECT_FALSE(layerB->bitFields.dirtyTransform);
  EXPECT_FALSE(layerC->bitFields.dirtyTransform);
  EXPECT_TRUE(layerA->bitFields.dirtyTransform);

  root->updateDirtyRegions();

  // Scenario 3: [D, B, C, A] -> [A, C, B, D]
  // Old positions in new order: A(3), C(2), B(1), D(0)
  // This is a decreasing sequence, LIS length is 1, so 3 layers should be dirty
  // One of A, C, B, D will be in LIS (the algorithm picks one), others are dirty
  std::vector<std::shared_ptr<Layer>> scenario3 = {layerA, layerC, layerB, layerD};
  root->setChildren(scenario3);

  EXPECT_EQ(root->children().size(), 4u);
  EXPECT_EQ(root->children()[0], layerA);
  EXPECT_EQ(root->children()[1], layerC);
  EXPECT_EQ(root->children()[2], layerB);
  EXPECT_EQ(root->children()[3], layerD);
  int dirtyCount = 0;
  if (layerA->bitFields.dirtyTransform) {
    dirtyCount++;
  }
  if (layerC->bitFields.dirtyTransform) {
    dirtyCount++;
  }
  if (layerB->bitFields.dirtyTransform) {
    dirtyCount++;
  }
  if (layerD->bitFields.dirtyTransform) {
    dirtyCount++;
  }
  EXPECT_EQ(dirtyCount, 3);

  root->updateDirtyRegions();

  // Scenario 4: No change - should be a no-op, no layers dirty
  std::vector<std::shared_ptr<Layer>> noChange = {layerA, layerC, layerB, layerD};
  root->setChildren(noChange);

  EXPECT_EQ(root->children().size(), 4u);
  EXPECT_EQ(root->children()[0], layerA);
  EXPECT_EQ(root->children()[1], layerC);
  EXPECT_EQ(root->children()[2], layerB);
  EXPECT_EQ(root->children()[3], layerD);
  EXPECT_FALSE(layerA->bitFields.dirtyTransform);
  EXPECT_FALSE(layerC->bitFields.dirtyTransform);
  EXPECT_FALSE(layerB->bitFields.dirtyTransform);
  EXPECT_FALSE(layerD->bitFields.dirtyTransform);

  // Scenario 5: Single element - edge case
  auto displayList2 = std::make_unique<DisplayList>();
  auto singleParent = static_cast<RootLayer*>(displayList2->root());
  auto singleLayer = Layer::Make();
  singleLayer->setName("Single");

  std::vector<std::shared_ptr<Layer>> single = {singleLayer};
  singleParent->setChildren(single);

  EXPECT_EQ(singleParent->children().size(), 1u);
  EXPECT_EQ(singleParent->children()[0], singleLayer);
  EXPECT_TRUE(singleLayer->bitFields.dirtyTransform);

  singleParent->updateDirtyRegions();

  // Reorder with same single element - no change, not dirty
  singleParent->setChildren(single);
  EXPECT_EQ(singleParent->children().size(), 1u);
  EXPECT_FALSE(singleLayer->bitFields.dirtyTransform);
}

TGFX_TEST(LayerTest, Layer3DContextAPI) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto renderRect = Rect::MakeWH(200, 200);
  float contentScale = 1.0f;
  auto colorSpace = ColorSpace::SRGB();

  // Test Render3DContext creation (contourMode = false)
  auto render3DContext =
      Layer3DContext::Make(false, context, renderRect, contentScale, colorSpace, nullptr);
  ASSERT_TRUE(render3DContext != nullptr);
  EXPECT_TRUE(render3DContext->isFinished());

  // Test beginRecording/endRecording cycle
  auto rotateMatrix = Matrix3D::MakeRotate({0, 1, 0}, 30);
  auto recordCanvas = render3DContext->beginRecording(rotateMatrix, true);
  ASSERT_TRUE(recordCanvas != nullptr);
  EXPECT_FALSE(render3DContext->isFinished());

  // Draw something
  Paint paint = {};
  paint.setColor(Color::Red());
  recordCanvas->drawRect(Rect::MakeXYWH(20, 20, 60, 60), paint);

  render3DContext->endRecording();
  EXPECT_TRUE(render3DContext->isFinished());

  // Test Contour3DContext creation (contourMode = true)
  auto contour3DContext =
      Layer3DContext::Make(true, context, renderRect, contentScale, colorSpace, nullptr);
  ASSERT_TRUE(contour3DContext != nullptr);
  EXPECT_TRUE(contour3DContext->isFinished());

  // Test nested recording (simulates nested preserve3D layers)
  auto transform1 = Matrix3D::MakeRotate({0, 1, 0}, 20);
  auto canvas1 = contour3DContext->beginRecording(transform1, true);
  EXPECT_FALSE(contour3DContext->isFinished());
  paint.setColor(Color::Blue());
  canvas1->drawRect(Rect::MakeXYWH(10, 10, 80, 80), paint);

  // Nested recording - tests recorder stack isolation
  auto transform2 = Matrix3D::MakeRotate({1, 0, 0}, 30);
  auto canvas2 = contour3DContext->beginRecording(transform2, true);
  ASSERT_TRUE(canvas2 != nullptr);
  EXPECT_NE(canvas1, canvas2);
  EXPECT_FALSE(contour3DContext->isFinished());
  paint.setColor(Color::Green());
  canvas2->drawRect(Rect::MakeXYWH(30, 30, 40, 40), paint);

  // End inner recording first
  contour3DContext->endRecording();
  EXPECT_FALSE(contour3DContext->isFinished());

  // End outer recording
  contour3DContext->endRecording();
  EXPECT_TRUE(contour3DContext->isFinished());
}

TGFX_TEST(LayerTest, Preserve3DNestedLayers) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);

  auto displayList = std::make_unique<DisplayList>();

  // Create parent layer with preserve3D
  auto parentLayer = SolidLayer::Make();
  parentLayer->setColor(Color::FromRGBA(200, 200, 200, 255));
  parentLayer->setWidth(180);
  parentLayer->setHeight(180);
  parentLayer->setMatrix(Matrix::MakeTrans(10, 10));
  parentLayer->setPreserve3D(true);

  // Create first child with 3D transform using perspective
  auto child1 = SolidLayer::Make();
  child1->setColor(Color::Red());
  child1->setWidth(60);
  child1->setHeight(60);
  {
    auto size = Size::Make(60, 60);
    auto anchor = Point::Make(0.5f, 0.5f);
    auto offsetToAnchor =
        Matrix3D::MakeTranslate(-anchor.x * size.width, -anchor.y * size.height, 0);
    auto invOffsetToAnchor =
        Matrix3D::MakeTranslate(anchor.x * size.width, anchor.y * size.height, 0);
    auto rotate = Matrix3D::MakeRotate({0, 1, 0}, 30);
    auto perspective = Matrix3D::I();
    perspective.setRowColumn(3, 2, -1.0f / 500.0f);
    auto origin = Matrix3D::MakeTranslate(30, 30, 0);
    child1->setMatrix3D(origin * invOffsetToAnchor * perspective * rotate * offsetToAnchor);
  }

  // Create second child with different 3D transform
  auto child2 = SolidLayer::Make();
  child2->setColor(Color::Blue());
  child2->setWidth(60);
  child2->setHeight(60);
  {
    auto size = Size::Make(60, 60);
    auto anchor = Point::Make(0.5f, 0.5f);
    auto offsetToAnchor =
        Matrix3D::MakeTranslate(-anchor.x * size.width, -anchor.y * size.height, 0);
    auto invOffsetToAnchor =
        Matrix3D::MakeTranslate(anchor.x * size.width, anchor.y * size.height, 0);
    auto rotate = Matrix3D::MakeRotate({0, 1, 0}, -30);
    auto perspective = Matrix3D::I();
    perspective.setRowColumn(3, 2, -1.0f / 500.0f);
    auto origin = Matrix3D::MakeTranslate(100, 100, 0);
    child2->setMatrix3D(origin * invOffsetToAnchor * perspective * rotate * offsetToAnchor);
  }

  parentLayer->addChild(child1);
  parentLayer->addChild(child2);
  displayList->root()->addChild(parentLayer);

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/Preserve3DNestedLayers"));
}

TGFX_TEST(LayerTest, Contour3DWithDropShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);

  auto displayList = std::make_unique<DisplayList>();

  // Create parent layer with preserve3D and drop shadow (showBehindLayer = false)
  // Parent is semi-transparent so we can see shadow through it
  auto parentLayer = SolidLayer::Make();
  parentLayer->setColor(Color::FromRGBA(200, 200, 200, 100));
  parentLayer->setWidth(150);
  parentLayer->setHeight(150);
  parentLayer->setMatrix(Matrix::MakeTrans(75, 75));

  auto dropShadow = DropShadowStyle::Make(10, 10, 8, 8, Color::FromRGBA(0, 0, 0, 200));
  dropShadow->setShowBehindLayer(false);
  parentLayer->setLayerStyles({dropShadow});

  // Create first child with 3D transform - positioned to extend outside parent
  auto child1 = SolidLayer::Make();
  child1->setColor(Color::FromRGBA(255, 0, 0, 200));
  child1->setWidth(80);
  child1->setHeight(80);
  child1->setPreserve3D(true);
  {
    auto size = Size::Make(80, 80);
    auto anchor = Point::Make(0.5f, 0.5f);
    auto offsetToAnchor =
        Matrix3D::MakeTranslate(-anchor.x * size.width, -anchor.y * size.height, 0);
    auto invOffsetToAnchor =
        Matrix3D::MakeTranslate(anchor.x * size.width, anchor.y * size.height, 0);
    auto rotate = Matrix3D::MakeRotate({0, 1, 0}, 40);
    auto perspective = Matrix3D::I();
    perspective.setRowColumn(3, 2, -1.0f / 300.0f);
    // Position at top-left, extending outside parent
    auto origin = Matrix3D::MakeTranslate(-20, -20, 0);
    child1->setMatrix3D(origin * invOffsetToAnchor * perspective * rotate * offsetToAnchor);
  }

  // Create second child with different 3D transform - positioned to extend outside parent
  auto child2 = SolidLayer::Make();
  child2->setColor(Color::FromRGBA(0, 0, 255, 200));
  child2->setWidth(80);
  child2->setHeight(80);
  {
    auto size = Size::Make(80, 80);
    auto anchor = Point::Make(0.5f, 0.5f);
    auto offsetToAnchor =
        Matrix3D::MakeTranslate(-anchor.x * size.width, -anchor.y * size.height, 0);
    auto invOffsetToAnchor =
        Matrix3D::MakeTranslate(anchor.x * size.width, anchor.y * size.height, 0);
    auto rotate = Matrix3D::MakeRotate({0, 1, 0}, -40);
    auto perspective = Matrix3D::I();
    perspective.setRowColumn(3, 2, -1.0f / 300.0f);
    // Position at bottom-right, extending outside parent
    auto origin = Matrix3D::MakeTranslate(90, 90, 0);
    child2->setMatrix3D(origin * invOffsetToAnchor * perspective * rotate * offsetToAnchor);
  }

  parentLayer->addChild(child1);
  parentLayer->addChild(child2);
  displayList->root()->addChild(parentLayer);

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/Contour3DWithDropShadow"));
}

}  // namespace tgfx
