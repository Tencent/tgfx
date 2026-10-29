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
#include "core/Matrix3DUtils.h"
#include "core/filters/GaussianBlurImageFilter.h"
#include "core/images/RasterizedImage.h"
#include "core/shaders/GradientShader.h"
#include "core/utils/MathExtra.h"
#include "layers/BackgroundHandler.h"
#include "layers/DrawArgs.h"
#include "layers/LayerStyleSource.h"
#include "layers/OpaqueContext.h"
#include "layers/RootLayer.h"
#include "layers/SubtreeCache.h"
#include "layers/TileCache.h"
#include "layers/compositing3d/Render3DContext.h"
#include "layers/contents/ComposeContent.h"
#include "layers/contents/MatrixContent.h"
#include "layers/contents/RRectsContent.h"
#include "layers/contents/RectsContent.h"
#include "layers/contents/TextContent.h"
#include "layers/processors/GlassRefractionFragmentProcessor.h"
#include "tgfx/core/Mesh.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/LayerRecorder.h"
#include "tgfx/layers/MeshLayer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/VectorLayer.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/filters/DropShadowFilter.h"
#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "tgfx/layers/layerstyles/GlassStyle.h"
#include "tgfx/layers/layerstyles/InnerShadowStyle.h"
#include "tgfx/layers/layerstyles/NoiseStyle.h"
#include "tgfx/layers/vectors/Ellipse.h"
#include "tgfx/layers/vectors/FillStyle.h"
#include "tgfx/layers/vectors/Gradient.h"
#include "tgfx/layers/vectors/ImagePattern.h"
#include "tgfx/layers/vectors/MergePath.h"
#include "tgfx/layers/vectors/Rectangle.h"
#include "tgfx/layers/vectors/Repeater.h"
#include "tgfx/layers/vectors/ShapePath.h"
#include "tgfx/layers/vectors/SolidColor.h"
#include "tgfx/layers/vectors/StrokeStyle.h"
#include "tgfx/layers/vectors/Text.h"
#include "tgfx/layers/vectors/VectorGroup.h"
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

  TGFX_PRIVATE_ACCESS(EXPECT_FALSE(parent->addChild(displayList->_root));
                      EXPECT_FALSE(parent->contains(displayList->_root));)

  EXPECT_FALSE(child->contains(parent));

  TGFX_PRIVATE_ACCESS(EXPECT_EQ(displayList->_root, displayList->_root);)

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

TGFX_TEST_PRIVATE(LayerTest, Layer_getTotalMatrix) {
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

  TGFX_PRIVATE_ACCESS(auto greatGrandsonTotalMatrix = greatGrandson->getGlobalMatrix();
                      EXPECT_EQ(greatGrandsonTotalMatrix, Matrix3D::MakeTranslate(30, 30, 0)));

  EXPECT_EQ(greatGrandson->matrix(), Matrix::MakeTrans(10, 10));
  EXPECT_EQ(grandChild->matrix(), Matrix::MakeTrans(10, 10));
  EXPECT_EQ(child->matrix(), Matrix::MakeTrans(10, 10));
  EXPECT_EQ(parent->matrix(), Matrix::MakeTrans(10, 10));

  auto rotateMat = Matrix::MakeRotate(45);
  greatGrandson->setMatrix(rotateMat * greatGrandson->matrix());

  TGFX_PRIVATE_ACCESS(
      auto greatGrandsonTotalMatrix2 = greatGrandson->getGlobalMatrix();
      auto grandChildTotalMatrix = grandChild->getGlobalMatrix(); EXPECT_FLOAT_EQ(
          greatGrandsonTotalMatrix2.getTranslateX(), grandChildTotalMatrix.getTranslateX());
      EXPECT_FLOAT_EQ(greatGrandsonTotalMatrix2.getTranslateY(),
                      grandChildTotalMatrix.getTranslateX() + 10.0f * std::sqrt(2.0f)));
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
  TGFX_PRIVATE_ACCESS(auto layer2GlobalMat = layerA2->getGlobalMatrix();)

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
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(pointEInGlobal, Point::Make(layer2GlobalMat.getTranslateX(),
                                                            layer2GlobalMat.getTranslateY() +
                                                                8.0f * std::sqrt(2.0f)));)

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
  innerShadow->setExcludeChildEffects(true);
  auto dropShadow = DropShadowStyle::Make(-20, -20, 0, 0, Color::Black());
  dropShadow->setShowBehindLayer(false);
  dropShadow->setExcludeChildEffects(true);
  shapeLayer->setLayerStyles({dropShadow, innerShadow});
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
  TGFX_PRIVATE_ACCESS(shaperLayer1->getGlobalMatrix().mapRect(&shaperLayer1Bounds);)
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
  TGFX_PRIVATE_ACCESS(shaperLayer2->getGlobalMatrix().mapRect(&shaperLayer2Bounds);)
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
  auto rRect = RRect::MakeRectXY(Rect::MakeXYWH(50, 200, 100, 100), 20.0f, 20.0f);
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
  auto rRect = RRect::MakeRectXY(rect, 10.0f, 10.0f);
  canvas->drawRRect(rRect, fillPaint);

  auto strokePaint = Paint();
  strokePaint.setStyle(PaintStyle::Stroke);
  strokePaint.setStrokeWidth(10.0f);
  strokePaint.setColor(Color::Red());

  Rect rect1 = Rect::MakeXYWH(300, 50, 200, 160);
  auto rRect1 = RRect::MakeRectXY(rect1, 10.0f, 10.0f);
  canvas->drawRRect(rRect1, strokePaint);

  Rect rect2 = Rect::MakeXYWH(600, 50, 200, 160);
  auto rRect2 = RRect::MakeRectXY(rect2, 15.0f, 10.0f);
  canvas->drawRRect(rRect2, strokePaint);

  Rect rect3 = Rect::MakeXYWH(50, 300, 200, 160);
  auto rRect3 = RRect::MakeRectXY(rect3, 100.0f, 150.0f);
  canvas->drawRRect(rRect3, strokePaint);

  Rect rect4 = Rect::MakeXYWH(300, 300, 200, 160);
  auto rRect4 = RRect::MakeRectXY(rect4, 50.0f, 10.0f);
  canvas->drawRRect(rRect4, strokePaint);

  auto strokePaint2 = Paint();
  strokePaint2.setStyle(PaintStyle::Stroke);
  strokePaint2.setStrokeWidth(50.0f);
  strokePaint2.setColor(Color::Red());

  Rect rect5 = Rect::MakeXYWH(600, 300, 200, 160);
  auto rRect5 = RRect::MakeRectXY(rect5, 20.f, 10.f);
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
  auto texture = context->gpu()->createTexture({200, 200, PixelFormat::RGBA_8888});
  ASSERT_TRUE(texture != nullptr);
  auto surface = Surface::MakeFrom(context, texture->getBackendTexture(), ImageOrigin::BottomLeft);

  // parent
  auto parentFrame = tgfx::Rect::MakeXYWH(60, 110, 40, 40);

  auto childFrame = tgfx::Rect::MakeWH(150, 150);
  auto childLayer = tgfx::ShapeLayer::Make();

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

TGFX_TEST_PRIVATE(LayerTest, ContourTest) {
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
  OpaqueContext allSolidContext;
  TGFX_PRIVATE_ACCESS(
      auto allSolidCanvas = allSolidContext.beginRecording();
      allSolidLayer->drawContour(drawArgs, allSolidCanvas, 1.0f, BlendMode::SrcOver));
  auto allSolidPicture = allSolidContext.finishRecordingAsPicture();
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(allSolidPicture->drawCount, 1u));

  // Case 2: Same geometry with all image shader fills should collect all (no dedup).
  auto allImageLayer = ShapeLayer::Make();
  path.reset();
  path.addRect(Rect::MakeXYWH(110, 10, 80, 80));
  allImageLayer->setPath(path);
  allImageLayer->addFillStyle(imageStyle1);
  allImageLayer->addFillStyle(imageStyle2);
  OpaqueContext allImageContext;
  TGFX_PRIVATE_ACCESS(
      auto allImageCanvas = allImageContext.beginRecording();
      allImageLayer->drawContour(drawArgs, allImageCanvas, 1.0f, BlendMode::SrcOver));
  auto allImagePicture = allImageContext.finishRecordingAsPicture();
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(allImagePicture->drawCount, 2u));

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
  OpaqueContext mixedFillsContext;
  auto mixedFillsCanvas = mixedFillsContext.beginRecording();
  TGFX_PRIVATE_ACCESS(
      mixedFillsLayer->drawContour(drawArgs, mixedFillsCanvas, 1.0f, BlendMode::SrcOver));
  auto mixedFillsPicture = mixedFillsContext.finishRecordingAsPicture();
  // Should be 1 (only first collected). Buggy single-pass would return 2.
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(mixedFillsPicture->drawCount, 1u));

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
  path.addRRect(RRect::MakeRectXY(Rect::MakeXYWH(120, 120, 60, 60), 10, 10));
  childLayer->setPath(path);
  childLayer->addFillStyle(ShapeStyle::Make(Color::Red()));
  childLayer->addFillStyle(ShapeStyle::Make(Color::Blue()));
  twoGroupsLayer->addChild(childLayer);
  OpaqueContext twoGroupsContext;
  auto twoGroupsCanvas = twoGroupsContext.beginRecording();
  TGFX_PRIVATE_ACCESS(
      twoGroupsLayer->drawContour(drawArgs, twoGroupsCanvas, 1.0f, BlendMode::SrcOver));
  auto twoGroupsPicture = twoGroupsContext.finishRecordingAsPicture();
  // Group 1: 2 (all image shaders), Group 2: 1 (deduped) = 3 total.
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(twoGroupsPicture->drawCount, 3u));

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
  OpaqueContext strokeTestContext;
  auto strokeTestCanvas = strokeTestContext.beginRecording();
  TGFX_PRIVATE_ACCESS(
      strokeTestLayer->drawContour(drawArgs, strokeTestCanvas, 1.0f, BlendMode::SrcOver));
  auto strokeTestPicture = strokeTestContext.finishRecordingAsPicture();
  // 1 (transparent fill) + 1 (strokes deduped) = 2 contours.
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(strokeTestPicture->drawCount, 2u));

  // Draw all to canvas for baseline comparison.
  auto rootLayer = ShapeLayer::Make();
  rootLayer->addChild(allSolidLayer);
  rootLayer->addChild(allImageLayer);
  rootLayer->addChild(mixedFillsLayer);
  rootLayer->addChild(twoGroupsLayer);
  rootLayer->addChild(strokeTestLayer);
  OpaqueContext allContext;
  auto allCanvas = allContext.beginRecording();
  TGFX_PRIVATE_ACCESS(rootLayer->drawContour(drawArgs, allCanvas, 1.0f, BlendMode::SrcOver));
  auto allPicture = allContext.finishRecordingAsPicture();
  // 1 + 2 + 1 + 3 + 2 = 9
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(allPicture->drawCount, 9u));

  surface = Surface::Make(context, 200, 300);
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->drawPicture(allPicture);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/ContourTest"));
}

TGFX_TEST_PRIVATE(LayerTest, ContourMatchesContent) {
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
  OpaqueContext opaqueContext;
  auto opaqueCanvas = opaqueContext.beginRecording();
  TGFX_PRIVATE_ACCESS(
      bool opaqueMatch = opaqueLayer->drawContour(drawArgs, opaqueCanvas, 1.0f, BlendMode::SrcOver);
      EXPECT_TRUE(opaqueMatch));
  opaqueContext.finishRecordingAsPicture();

  // Case 2: Transparent fill (alpha = 0) should not match content.
  auto transparentLayer = ShapeLayer::Make();
  transparentLayer->setPath(path);
  transparentLayer->addFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 0, 0, 0)));
  OpaqueContext transparentContext;
  auto transparentCanvas = transparentContext.beginRecording();
  TGFX_PRIVATE_ACCESS(bool transparentMatch = transparentLayer->drawContour(
                          drawArgs, transparentCanvas, 1.0f, BlendMode::SrcOver);
                      EXPECT_FALSE(transparentMatch));
  transparentContext.finishRecordingAsPicture();

  // Case 3: Image shader fill should match content.
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  auto imageShader = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Repeat);
  auto imageLayer = ShapeLayer::Make();
  imageLayer->setPath(path);
  imageLayer->addFillStyle(ShapeStyle::Make(imageShader));
  OpaqueContext imageContext;
  auto imageCanvas = imageContext.beginRecording();
  TGFX_PRIVATE_ACCESS(bool imageMatch =
                          imageLayer->drawContour(drawArgs, imageCanvas, 1.0f, BlendMode::SrcOver);
                      EXPECT_TRUE(imageMatch));
  imageContext.finishRecordingAsPicture();

  // Case 4: Layer with filter should not match content (child layer check).
  auto parentLayer = Layer::Make();
  auto filterLayer = ShapeLayer::Make();
  filterLayer->setPath(path);
  filterLayer->addFillStyle(ShapeStyle::Make(Color::Red()));
  filterLayer->setFilters({BlurFilter::Make(5, 5)});
  parentLayer->addChild(filterLayer);
  OpaqueContext filterContext;
  auto filterCanvas = filterContext.beginRecording();
  TGFX_PRIVATE_ACCESS(
      bool filterMatch = parentLayer->drawContour(drawArgs, filterCanvas, 1.0f, BlendMode::SrcOver);
      EXPECT_FALSE(filterMatch));
  filterContext.finishRecordingAsPicture();

  // Case 5: Layer with layerStyle should not match content (child layer check).
  auto parentLayer2 = Layer::Make();
  auto styleLayer = ShapeLayer::Make();
  styleLayer->setPath(path);
  styleLayer->addFillStyle(ShapeStyle::Make(Color::Red()));
  styleLayer->setLayerStyles({DropShadowStyle::Make(5, 5, 0, 5, Color::Black())});
  parentLayer2->addChild(styleLayer);
  OpaqueContext styleContext;
  auto styleCanvas = styleContext.beginRecording();
  TGFX_PRIVATE_ACCESS(
      bool styleMatch = parentLayer2->drawContour(drawArgs, styleCanvas, 1.0f, BlendMode::SrcOver);
      EXPECT_FALSE(styleMatch));
  styleContext.finishRecordingAsPicture();

  // Case 6: Gradient shader should check isOpaque().
  auto gradientShader = Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(100, 100),
                                                   {Color::Red(), Color::Blue()}, {0.0f, 1.0f});
  auto gradientLayer = ShapeLayer::Make();
  gradientLayer->setPath(path);
  gradientLayer->addFillStyle(ShapeStyle::Make(gradientShader));
  OpaqueContext gradientContext;
  auto gradientCanvas = gradientContext.beginRecording();
  TGFX_PRIVATE_ACCESS(bool gradientMatch = gradientLayer->drawContour(drawArgs, gradientCanvas,
                                                                      1.0f, BlendMode::SrcOver);
                      // Gradient with opaque colors should match.
                      EXPECT_TRUE(gradientMatch));
  gradientContext.finishRecordingAsPicture();
}

TGFX_TEST_PRIVATE(LayerTest, ContourContainsOpaqueBounds) {
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

  OpaqueContext contourContext;
  auto canvas = contourContext.beginRecording();
  TGFX_PRIVATE_ACCESS(parentLayer->drawContour(drawArgs, canvas, 1.0f, BlendMode::SrcOver));
  auto picture = contourContext.finishRecordingAsPicture();
  // Parent contour drawn, child skipped due to containsOpaqueBounds optimization.
  // Only 1 draw call for parent.
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(picture->drawCount, 1u));

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

  OpaqueContext contourContext2;
  auto canvas2 = contourContext2.beginRecording();
  TGFX_PRIVATE_ACCESS(parentLayer2->drawContour(drawArgs, canvas2, 1.0f, BlendMode::SrcOver));
  auto picture2 = contourContext2.finishRecordingAsPicture();
  // Both parent and child contours should be drawn.
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(picture2->drawCount, 2u));
}

TGFX_TEST_PRIVATE(LayerTest, GetContourImage) {
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
  TGFX_PRIVATE_ACCESS(auto contourImage = shapeLayer->getContentContourImage(
                          drawArgs, 1.0f, &offset, &contourMatchesContent);
                      EXPECT_TRUE(contourImage != nullptr); EXPECT_EQ(offset.x, 10.0f);
                      EXPECT_EQ(offset.y, 10.0f); EXPECT_EQ(contourImage->width(), 80);
                      EXPECT_EQ(contourImage->height(), 80); EXPECT_TRUE(contourMatchesContent);
                      // Draw the contour image to verify.
                      auto canvas = surface->getCanvas();
                      canvas->clear(); canvas->drawImage(contourImage, offset.x, offset.y));
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/GetContourImage"));
}

TGFX_TEST_PRIVATE(LayerTest, ContourWithMask) {
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

  OpaqueContext contourContext;
  auto canvas = contourContext.beginRecording();
  TGFX_PRIVATE_ACCESS(shapeLayer->drawContour(drawArgs, canvas, 1.0f, BlendMode::SrcOver));
  auto picture = contourContext.finishRecordingAsPicture();
  // The mask clips the shape, so we should have 1 contour (the masked shape).
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(picture->drawCount, 1u));

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
  auto glassRRect = RRect::MakeRectXY(Rect::MakeXYWH(10, 10, 80, 80), 40, 40);
  glassPath.addRRect(glassRRect);
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

  // Test offscreen rendering blending with 3D background layer
  {
    auto displayList3D = std::make_unique<DisplayList>();
    auto surface3D = Surface::Make(context, 200, 200);

    auto imageLayer2D = ImageLayer::Make();
    auto image2D = MakeImage("resources/assets/HappyNewYear.png");
    imageLayer2D->setImage(image2D);
    imageLayer2D->setMatrix(Matrix::MakeScale(0.5f));
    displayList3D->root()->addChild(imageLayer2D);

    auto imageLayer3D = ImageLayer::Make();
    auto image3D = MakeImage("resources/apitest/imageReplacement.jpg");
    imageLayer3D->setImage(image3D);
    auto imageSize =
        Size::Make(static_cast<float>(image3D->width()), static_cast<float>(image3D->height()));
    auto anchor = Point::Make(0.5f, 0.5f);
    auto offsetToAnchorMatrix =
        Matrix3D::MakeTranslate(-anchor.x * imageSize.width, -anchor.y * imageSize.height, 0.f);
    auto invOffsetToAnchorMatrix =
        Matrix3D::MakeTranslate(anchor.x * imageSize.width, anchor.y * imageSize.height, 0.f);
    auto modelMatrix = Matrix3D::MakeRotate({0.f, 1.f, 0.f}, 45.f);
    auto perspectiveMatrix = Matrix3D::I();
    perspectiveMatrix.setRowColumn(3, 2, -1.f / 200.f);
    auto origin = Point::Make(100 - anchor.x * imageSize.width, 100 - anchor.y * imageSize.height);
    auto originTranslateMatrix = Matrix3D::MakeTranslate(origin.x, origin.y, 0.f);
    auto transformMatrix = originTranslateMatrix * invOffsetToAnchorMatrix * perspectiveMatrix *
                           modelMatrix * offsetToAnchorMatrix;
    imageLayer3D->setMatrix3D(transformMatrix);
    displayList3D->root()->addChild(imageLayer3D);

    auto layerWithMask = SolidLayer::Make();
    layerWithMask->setColor(Color::FromRGBA(255, 0, 0, 26));
    layerWithMask->setWidth(imageSize.width);
    layerWithMask->setHeight(imageSize.height);
    layerWithMask->setBlendMode(BlendMode::SrcOver);
    imageLayer3D->addChild(layerWithMask);

    auto blendLayer = SolidLayer::Make();
    blendLayer->setColor(Color::FromRGBA(0, 255, 0, 128));
    blendLayer->setWidth(imageSize.width);
    blendLayer->setHeight(imageSize.height);
    blendLayer->setBlendMode(BlendMode::SrcIn);
    layerWithMask->addChild(blendLayer);

    auto imageMaskLayer = ImageLayer::Make();
    auto maskImage = MakeImage("resources/apitest/test_timestretch.png");
    imageMaskLayer->setImage(maskImage);
    auto maskScale = Matrix::MakeScale(imageSize.width / static_cast<float>(maskImage->width()),
                                       imageSize.height / static_cast<float>(maskImage->height()));
    imageMaskLayer->setMatrix(maskScale);
    layerWithMask->addChild(imageMaskLayer);
    layerWithMask->setMask(imageMaskLayer);

    displayList3D->render(surface3D.get());
    EXPECT_TRUE(Baseline::Compare(surface3D, "LayerTest/Matrix_3D_Offscreen_Blend"));
  }
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

TGFX_TEST_PRIVATE(LayerTest, LayerRecorder) {
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
    recorder.addRect(Rect::MakeXYWH(10, 10, 50, 50), &redPaint);
    recorder.addRect(Rect::MakeXYWH(70, 10, 50, 50), &redPaint);
    recorder.addRect(Rect::MakeXYWH(130, 10, 50, 50), &redPaint);
    recorder.addRect(Rect::MakeXYWH(10, 70, 50, 50), &bluePaint);
    TGFX_PRIVATE_ACCESS(
        auto content = recorder.finishRecording(); ASSERT_TRUE(content != nullptr);
        // Should be ComposeContent with 2 items: RectsContent (3 rects) + RectContent (1 rect)
        EXPECT_EQ(content->type(), LayerContent::Type::Compose);
        auto composeContent = static_cast<ComposeContent*>(content.get());
        EXPECT_EQ(composeContent->contents.size(), 2u);
        EXPECT_EQ(composeContent->contents[0]->type(), LayerContent::Type::Rects);
        auto rectsContent = static_cast<RectsContent*>(composeContent->contents[0].get());
        EXPECT_EQ(rectsContent->rects.size(), 3u);
        EXPECT_EQ(composeContent->contents[1]->type(), LayerContent::Type::Rect);
        content->drawDefault(surface->getCanvas(), 1.0f, true));
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerRecorder_MultipleRects"));
  }

  // Test 2: Multiple rrects with same paint should merge into RRectsContent
  {
    auto surface = Surface::Make(context, 200, 150);
    LayerRecorder recorder = {};
    auto rRect1 = RRect::MakeRectXY(Rect::MakeXYWH(10, 10, 50, 50), 10, 10);
    auto rRect2 = RRect::MakeRectXY(Rect::MakeXYWH(70, 10, 50, 50), 10, 10);
    auto rRect3 = RRect::MakeRectXY(Rect::MakeXYWH(130, 10, 50, 50), 10, 10);
    auto rRect4 = RRect::MakeRectXY(Rect::MakeXYWH(10, 70, 50, 50), 10, 10);
    recorder.addRRect(rRect1, &redPaint);
    recorder.addRRect(rRect2, &redPaint);
    recorder.addRRect(rRect3, &redPaint);
    recorder.addRRect(rRect4, &bluePaint);
    TGFX_PRIVATE_ACCESS(
        auto content = recorder.finishRecording(); ASSERT_TRUE(content != nullptr);
        // Should be ComposeContent with 2 items: RRectsContent (3 rrects) + RRectContent (1 rrect)
        EXPECT_EQ(content->type(), LayerContent::Type::Compose);
        auto composeContent = static_cast<ComposeContent*>(content.get());
        EXPECT_EQ(composeContent->contents.size(), 2u);
        EXPECT_EQ(composeContent->contents[0]->type(), LayerContent::Type::RRects);
        auto rrectsContent = static_cast<RRectsContent*>(composeContent->contents[0].get());
        EXPECT_EQ(rrectsContent->rRects.size(), 3u);
        EXPECT_EQ(composeContent->contents[1]->type(), LayerContent::Type::RRect);
        content->drawDefault(surface->getCanvas(), 1.0f, true));
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerRecorder_MultipleRRects"));
  }

  // Test 3: Merge ovals with the same paint into RRectsContent and emit a separate RRectContent for the differing paint.
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
    recorder.addPath(path1, &redPaint);
    recorder.addPath(path2, &redPaint);
    recorder.addPath(path3, &redPaint);
    recorder.addPath(path4, &bluePaint);
    TGFX_PRIVATE_ACCESS(
        auto content = recorder.finishRecording(); ASSERT_TRUE(content != nullptr);
        // Should be ComposeContent with 2 items: RRectsContent (3 red ovals) + RRectContent
        // (1 blue oval)
        EXPECT_EQ(content->type(), LayerContent::Type::Compose);
        auto composeContent = static_cast<ComposeContent*>(content.get());
        ASSERT_TRUE(composeContent->contents.size() == 2u);
        EXPECT_EQ(composeContent->contents[0]->type(), LayerContent::Type::RRects);
        EXPECT_EQ(composeContent->contents[1]->type(), LayerContent::Type::RRect);
        content->drawDefault(surface->getCanvas(), 1.0f, true));
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerRecorder_MultiplePaths"));
  }

  // Test 4: Mixed shapes with same paint should create separate contents
  {
    auto surface = Surface::Make(context, 200, 100);
    LayerRecorder recorder = {};
    recorder.addRect(Rect::MakeXYWH(10, 10, 50, 50), &redPaint);
    auto rRect = RRect::MakeRectXY(Rect::MakeXYWH(70, 10, 50, 50), 10, 10);
    recorder.addRRect(rRect, &redPaint);
    Path path = {};
    path.addOval(Rect::MakeXYWH(130, 10, 50, 50));
    recorder.addPath(path, &redPaint);
    TGFX_PRIVATE_ACCESS(
        auto content = recorder.finishRecording(); ASSERT_TRUE(content != nullptr);
        // Should be ComposeContent with 2 items: RectContent + RRectsContent (rrect+oval merged)
        EXPECT_EQ(content->type(), LayerContent::Type::Compose);
        auto composeContent = static_cast<ComposeContent*>(content.get());
        EXPECT_EQ(composeContent->contents.size(), 2u);
        EXPECT_EQ(composeContent->contents[0]->type(), LayerContent::Type::Rect);
        EXPECT_EQ(composeContent->contents[1]->type(), LayerContent::Type::RRects);
        content->drawDefault(surface->getCanvas(), 1.0f, true));
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
    recorder.addTextBlob(textBlob, &redPaint, 50, 100);
    TGFX_PRIVATE_ACCESS(auto content = recorder.finishRecording(); ASSERT_TRUE(content != nullptr);
                        // Should be single TextContent
                        EXPECT_EQ(content->type(), LayerContent::Type::Text);
                        auto textContent = static_cast<TextContent*>(content.get());
                        EXPECT_EQ(textContent->offset, Point::Make(50, 100));
                        content->drawDefault(surface->getCanvas(), 1.0f, true));
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerRecorder_TextBlobWithOffset"));
  }

  // Test 6: Line path with fill should be ignored
  {
    auto surface = Surface::Make(context, 200, 150);
    LayerRecorder recorder = {};
    Path linePath = {};
    linePath.moveTo(10, 10);
    linePath.lineTo(100, 100);
    recorder.addPath(linePath, &redPaint);  // Fill style, should be ignored
    recorder.addRect(Rect::MakeXYWH(10, 70, 50, 50), &bluePaint);
    TGFX_PRIVATE_ACCESS(auto content = recorder.finishRecording(); ASSERT_TRUE(content != nullptr);
                        // Should be single RectContent (line path ignored)
                        EXPECT_EQ(content->type(), LayerContent::Type::Rect);
                        content->drawDefault(surface->getCanvas(), 1.0f, true));
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerRecorder_LinePathFill"));
  }

  // Test 7: Line path with stroke should be converted to rect
  {
    auto surface = Surface::Make(context, 200, 100);
    LayerRecorder recorder = {};
    Path linePath = {};
    linePath.moveTo(10, 50);
    linePath.lineTo(190, 50);
    recorder.addPath(linePath, &strokePaint);
    TGFX_PRIVATE_ACCESS(auto content = recorder.finishRecording(); ASSERT_TRUE(content != nullptr);
                        // Should be single RectContent (line converted to rect)
                        EXPECT_EQ(content->type(), LayerContent::Type::Rect);
                        content->drawDefault(surface->getCanvas(), 1.0f, true));
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerRecorder_LinePathStroke"));
  }

  // Test 8: Single rect should be RectContent, not RectsContent
  {
    LayerRecorder recorder = {};
    recorder.addRect(Rect::MakeXYWH(10, 10, 50, 50), &redPaint);
    TGFX_PRIVATE_ACCESS(auto content = recorder.finishRecording(); ASSERT_TRUE(content != nullptr);
                        EXPECT_EQ(content->type(), LayerContent::Type::Rect));
  }

  // Test 9: Single rrect should be RRectContent, not RRectsContent
  {
    LayerRecorder recorder = {};
    auto rRect = RRect::MakeRectXY(Rect::MakeXYWH(10, 10, 50, 50), 10, 10);
    recorder.addRRect(rRect, &redPaint);
    TGFX_PRIVATE_ACCESS(auto content = recorder.finishRecording(); ASSERT_TRUE(content != nullptr);
                        EXPECT_EQ(content->type(), LayerContent::Type::RRect));
  }

  // Test 10: RRect with zero radius should be converted to Rect
  {
    LayerRecorder recorder = {};
    auto rRect = RRect::MakeRectXY(Rect::MakeXYWH(10, 10, 50, 50), 0, 0);
    recorder.addRRect(rRect, &redPaint);
    TGFX_PRIVATE_ACCESS(auto content = recorder.finishRecording(); ASSERT_TRUE(content != nullptr);
                        EXPECT_EQ(content->type(), LayerContent::Type::Rect));
  }
}

TGFX_TEST_PRIVATE(LayerTest, LayerRecorderMatrix) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  LayerPaint redFill = {};
  redFill.color = Color::Red();

  LayerPaint blueStroke = {};
  blueStroke.color = Color::Blue();
  blueStroke.style = PaintStyle::Stroke;
  blueStroke.stroke = Stroke(8.0f);

  LayerPaint greenFill = {};
  greenFill.color = Color::Green();

  // Test 1: Same matrix merges into MatrixContent wrapping Rects
  auto matrix1 = Matrix::MakeRotate(30, 50, 50) * Matrix::MakeScale(1.5f, 0.7f);
  LayerRecorder recorder1 = {};
  recorder1.setMatrix(matrix1);
  recorder1.addRect(Rect::MakeXYWH(10, 10, 50, 50), &redFill);
  recorder1.addRect(Rect::MakeXYWH(80, 10, 50, 50), &redFill);
  std::unique_ptr<LayerContent> content1;
  TGFX_PRIVATE_ACCESS(content1 = recorder1.finishRecording();
                      EXPECT_EQ(content1->type(), LayerContent::Type::Matrix);
                      auto matrixContent = static_cast<MatrixContent*>(content1.get());
                      EXPECT_EQ(matrixContent->content->type(), LayerContent::Type::Rects));

  // Test 2: Different matrices create Compose
  auto matrix2 = Matrix::MakeRotate(60, 50, 50);
  LayerRecorder recorder2 = {};
  recorder2.setMatrix(matrix1);
  recorder2.addRect(Rect::MakeXYWH(10, 10, 50, 50), &blueStroke);
  recorder2.setMatrix(matrix2);
  recorder2.addRect(Rect::MakeXYWH(80, 10, 50, 50), &blueStroke);
  std::unique_ptr<LayerContent> content2;
  TGFX_PRIVATE_ACCESS(content2 = recorder2.finishRecording();
                      EXPECT_EQ(content2->type(), LayerContent::Type::Compose);
                      auto composeContent = static_cast<ComposeContent*>(content2.get());
                      EXPECT_EQ(composeContent->contents.size(), 2u);
                      EXPECT_EQ(composeContent->contents[0]->type(), LayerContent::Type::Matrix);
                      EXPECT_EQ(composeContent->contents[1]->type(), LayerContent::Type::Matrix));

  // Test 3: Nullopt matrix should not create MatrixContent
  LayerRecorder recorder3 = {};
  recorder3.addRect(Rect::MakeXYWH(10, 10, 50, 50), &greenFill);
  std::unique_ptr<LayerContent> content3;
  TGFX_PRIVATE_ACCESS(content3 = recorder3.finishRecording();
                      EXPECT_EQ(content3->type(), LayerContent::Type::Rect));

  // Test 4: Identity matrix should not create MatrixContent
  LayerRecorder recorder4 = {};
  recorder4.setMatrix(Matrix::I());
  recorder4.addRect(Rect::MakeXYWH(10, 10, 50, 50), &blueStroke);
  std::unique_ptr<LayerContent> content4;
  TGFX_PRIVATE_ACCESS(content4 = recorder4.finishRecording();
                      EXPECT_EQ(content4->type(), LayerContent::Type::Rect));

  // Test 5: getTightBounds with transformed content
  auto bounds = content1->getTightBounds(Matrix::I(), false);
  bounds.roundOut();
  EXPECT_EQ(bounds, Rect::MakeLTRB(23, -5, 198, 116));

  // Test 6: hitTestPoint with rotated content
  LayerRecorder recorder5 = {};
  recorder5.setMatrix(Matrix::MakeRotate(45, 50, 50));
  recorder5.addRect(Rect::MakeXYWH(0, 0, 100, 100), &greenFill);
  std::unique_ptr<LayerContent> content5;
  TGFX_PRIVATE_ACCESS(content5 = recorder5.finishRecording());
  EXPECT_TRUE(content5->hitTestPoint(50, 50, false));
  EXPECT_FALSE(content5->hitTestPoint(0, 0, false));
  EXPECT_TRUE(content5->hitTestPoint(50, -15, false));

  // Test 7: Render all contents
  auto surface = Surface::Make(context, 600, 200);
  auto canvas = surface->getCanvas();
  canvas->concat(Matrix::MakeTrans(0, 50));
  content1->drawDefault(canvas, 1.0f, true);
  canvas->concat(Matrix::MakeTrans(200, 0));
  content2->drawDefault(canvas, 1.0f, true);
  canvas->concat(Matrix::MakeTrans(150, 0));
  content3->drawDefault(canvas, 1.0f, true);
  canvas->concat(Matrix::MakeTrans(50, 0));
  content4->drawDefault(canvas, 1.0f, true);
  canvas->concat(Matrix::MakeTrans(50, 0));
  content5->drawDefault(canvas, 1.0f, true);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/LayerRecorderMatrix"));

  // Test 8: MatrixShape with uniform scale should not affect stroke width.
  // Left: Shape wrapped with ApplyMatrix(scale=2), Right: Directly scaled rect (reference).
  // Both should have the same stroke width of 10.
  LayerPaint strokePaint = {};
  strokePaint.color = Color::Blue();
  strokePaint.style = PaintStyle::Stroke;
  strokePaint.stroke = Stroke(10.0f);

  Path rectPath = {};
  rectPath.addRect(Rect::MakeWH(50, 50));
  auto scaledShape = Shape::ApplyMatrix(Shape::MakeFrom(rectPath), Matrix::MakeScale(2.0f));
  LayerRecorder recorder6 = {};
  recorder6.addShape(scaledShape, &strokePaint);
  std::unique_ptr<LayerContent> content6;
  TGFX_PRIVATE_ACCESS(content6 = recorder6.finishRecording());

  LayerRecorder recorder7 = {};
  recorder7.addRect(Rect::MakeWH(100, 100), &strokePaint);
  std::unique_ptr<LayerContent> content7;
  TGFX_PRIVATE_ACCESS(content7 = recorder7.finishRecording());

  auto surface2 = Surface::Make(context, 350, 200);
  auto canvas2 = surface2->getCanvas();
  canvas2->concat(Matrix::MakeTrans(50, 50));
  content6->drawDefault(canvas2, 1.0f, true);
  canvas2->concat(Matrix::MakeTrans(150, 0));
  content7->drawDefault(canvas2, 1.0f, true);
  EXPECT_TRUE(Baseline::Compare(surface2, "LayerTest/LayerRecorderMatrixStroke"));
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
  TGFX_PRIVATE_ACCESS(
      auto rootLayer = RootLayer::Make(); rootLayer->_root = rootLayer.get();

      std::vector<std::shared_ptr<Layer>> withRoot = {layerA, rootLayer, layerB};
      parent->setChildren(withRoot);

      // Root layer should be rejected, only layerA and layerB should be added
      EXPECT_EQ(parent->children().size(), 2u); EXPECT_EQ(parent->children()[0], layerA);
      EXPECT_EQ(parent->children()[1], layerB); EXPECT_FALSE(parent->contains(rootLayer));)

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

TGFX_TEST_PRIVATE(LayerTest, SetChildrenLISValidation) {
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
  TGFX_PRIVATE_ACCESS(EXPECT_TRUE(layerD->bitFields.dirtyTransform);
                      EXPECT_FALSE(layerA->bitFields.dirtyTransform);
                      EXPECT_FALSE(layerB->bitFields.dirtyTransform);
                      EXPECT_FALSE(layerC->bitFields.dirtyTransform));

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
  TGFX_PRIVATE_ACCESS(EXPECT_FALSE(layerD->bitFields.dirtyTransform);
                      EXPECT_FALSE(layerB->bitFields.dirtyTransform);
                      EXPECT_FALSE(layerC->bitFields.dirtyTransform);
                      EXPECT_TRUE(layerA->bitFields.dirtyTransform));

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
  TGFX_PRIVATE_ACCESS(
      if (layerA->bitFields.dirtyTransform) {
        dirtyCount++;
      } if (layerC->bitFields.dirtyTransform) {
        dirtyCount++;
      } if (layerB->bitFields.dirtyTransform) {
        dirtyCount++;
      } if (layerD->bitFields.dirtyTransform) { dirtyCount++; });
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
  TGFX_PRIVATE_ACCESS(EXPECT_FALSE(layerA->bitFields.dirtyTransform);
                      EXPECT_FALSE(layerC->bitFields.dirtyTransform);
                      EXPECT_FALSE(layerB->bitFields.dirtyTransform);
                      EXPECT_FALSE(layerD->bitFields.dirtyTransform));

  // Scenario 5: Single element - edge case
  auto displayList2 = std::make_unique<DisplayList>();
  auto singleParent = static_cast<RootLayer*>(displayList2->root());
  auto singleLayer = Layer::Make();
  singleLayer->setName("Single");

  std::vector<std::shared_ptr<Layer>> single = {singleLayer};
  singleParent->setChildren(single);

  EXPECT_EQ(singleParent->children().size(), 1u);
  EXPECT_EQ(singleParent->children()[0], singleLayer);
  TGFX_PRIVATE_ACCESS(EXPECT_TRUE(singleLayer->bitFields.dirtyTransform));

  singleParent->updateDirtyRegions();

  // Reorder with same single element - no change, not dirty
  singleParent->setChildren(single);
  EXPECT_EQ(singleParent->children().size(), 1u);
  TGFX_PRIVATE_ACCESS(EXPECT_FALSE(singleLayer->bitFields.dirtyTransform));
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
  auto surface = Surface::Make(context, 600, 600);

  auto displayList = std::make_unique<DisplayList>();
  displayList->setZoomScale(2.0f);

  // Create parent layer with preserve3D and drop shadow (showBehindLayer = false)
  // Parent is semi-transparent so we can see shadow through it
  auto parentLayer = SolidLayer::Make();
  parentLayer->setColor(Color::FromRGBA(200, 200, 200, 100));
  parentLayer->setWidth(150);
  parentLayer->setHeight(150);
  parentLayer->setMatrix(Matrix::MakeTrans(75, 75));
  parentLayer->setPreserve3D(true);

  auto dropShadow = DropShadowStyle::Make(10, 10, 8, 8, Color::FromRGBA(0, 255, 0, 255));
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

TGFX_TEST(LayerTest, RootLayerBackgroundColorWithBlurBackground) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 200, 200);
  auto displayList = std::make_unique<DisplayList>();

  // Set background color on display list (which sets it on root layer)
  auto backgroundColor = Color::FromRGBA(255, 0, 0, 128);  // Semi-transparent red
  displayList->setBackgroundColor(backgroundColor);
  EXPECT_EQ(displayList->backgroundColor(), backgroundColor);

  // Add a bottom layer to provide content that will be blurred
  auto bottomLayer = ShapeLayer::Make();
  Path bottomPath = {};
  bottomPath.addRect(Rect::MakeXYWH(60.0f, 60.0f, 80.0f, 80.0f));
  bottomLayer->setPath(bottomPath);
  bottomLayer->setFillStyle(
      ShapeStyle::Make(Color::FromRGBA(0, 0, 255, 255)));  // Blue background content
  displayList->root()->addChild(bottomLayer);

  // Add a shape layer with background blur to exercise the BackgroundCapturer / BackgroundConsumer pipeline.
  // Position it so part covers bottomLayer and part covers pure background
  auto shapeLayer = ShapeLayer::Make();
  Path path = {};
  path.addRect(Rect::MakeXYWH(40.0f, 40.0f, 80.0f, 80.0f));
  shapeLayer->setPath(path);
  shapeLayer->setFillStyle(
      ShapeStyle::Make(Color::FromRGBA(255, 255, 255, 128)));  // Semi-transparent white

  // Add background blur style to trigger the blur background rendering
  auto backgroundBlur = BackgroundBlurStyle::Make(10.0f, 10.0f);
  shapeLayer->setLayerStyles({backgroundBlur});

  displayList->root()->addChild(shapeLayer);

  // Render the display list. RootLayer::drawLayer feeds the background-blur capture pass via
  // DisplayList::captureBackgrounds, which internally creates a BackgroundSource.
  displayList->render(surface.get());

  // Compare with baseline to verify the background color is correctly drawn
  // to both the main canvas and blur background canvas
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/RootLayerBackgroundColorWithBlurBackground"));
}

TGFX_TEST(LayerTest, MeshLayer) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  auto displayList = std::make_unique<DisplayList>();

  // Test 1: MeshLayer with vertex colors
  auto meshLayer1 = MeshLayer::Make();
  EXPECT_EQ(meshLayer1->type(), LayerType::Mesh);
  Point positions1[] = {{100, 50}, {50, 150}, {150, 150}};
  Color colors1[] = {Color::Red(), Color::Green(), Color::Blue()};
  auto mesh1 = Mesh::MakeCopy(MeshTopology::Triangles, 3, positions1, nullptr, colors1);
  meshLayer1->setMesh(mesh1);
  displayList->root()->addChild(meshLayer1);

  // Test 2: MeshLayer with fill style
  auto meshLayer2 = MeshLayer::Make();
  Path path = {};
  path.addRoundRect(Rect::MakeXYWH(150, 150, 120, 120), 15, 15);
  auto mesh2 = Mesh::MakeFromPath(path);
  meshLayer2->setMesh(mesh2);
  meshLayer2->setFillStyle(ShapeStyle::Make(Color::Blue(), BlendMode::SrcOver));
  displayList->root()->addChild(meshLayer2);

  // Test bounds
  auto bounds1 = meshLayer1->getBounds();
  // Triangle with vertices at (100,50), (50,150), (150,150)
  // Bounds: (50, 50, 150, 150)
  EXPECT_FLOAT_EQ(bounds1.left, 50);
  EXPECT_FLOAT_EQ(bounds1.top, 50);
  EXPECT_FLOAT_EQ(bounds1.right, 150);
  EXPECT_FLOAT_EQ(bounds1.bottom, 150);

  auto bounds2 = meshLayer2->getBounds();
  EXPECT_FLOAT_EQ(bounds2.left, 150);
  EXPECT_FLOAT_EQ(bounds2.top, 150);
  EXPECT_FLOAT_EQ(bounds2.right, 270);
  EXPECT_FLOAT_EQ(bounds2.bottom, 270);

  // Test fillStyles modification
  EXPECT_EQ(meshLayer2->fillStyles().size(), 1u);
  meshLayer2->addFillStyle(ShapeStyle::Make(Color::Red()));
  EXPECT_EQ(meshLayer2->fillStyles().size(), 2u);
  meshLayer2->removeFillStyles();
  EXPECT_EQ(meshLayer2->fillStyles().size(), 0u);
  meshLayer2->setFillStyle(ShapeStyle::Make(Color::Blue()));
  EXPECT_EQ(meshLayer2->fillStyles().size(), 1u);

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/MeshLayer"));
}

TGFX_TEST(LayerTest, BackgroundBlurWithFilter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  auto solidLayer = tgfx::SolidLayer::Make();
  solidLayer->setWidth(302);
  solidLayer->setHeight(227);
  solidLayer->setColor(tgfx::Color::White());

  auto backgroundBlur = tgfx::BackgroundBlurStyle::Make(4, 4);
  auto innerShadow =
      tgfx::InnerShadowStyle::Make(75, 60, 0, 0, tgfx::Color::FromRGBA(255, 0, 0, 60));
  solidLayer->setLayerStyles({backgroundBlur, innerShadow});

  auto blur = tgfx::BlurFilter::Make(20, 20);
  solidLayer->setFilters({blur});

  auto rootLayer = tgfx::Layer::Make();
  rootLayer->addChild(solidLayer);
  DisplayList displayList;
  displayList.root()->addChild(rootLayer);
  displayList.setRenderMode(RenderMode::Tiled);
  displayList.render(surface.get());

  displayList.setZoomScale(1.5);
  displayList.setContentOffset(-10, -10);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/BackgroundBlurWithFilter_Tiled"));

  displayList.setRenderMode(RenderMode::Partial);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/BackgroundBlurWithFilter_Partial"));
}

TGFX_TEST(LayerTest, BackgroundBlurAlignment) {
  // A checker background gives grid lines with exact positions, so any offset or coverage loss
  // in the background snapshot chain shows up as a discontinuity between the blurred grid
  // inside the layer and the sharp grid outside it. The layer also carries a LayerFilter,
  // which routes the capture pass through the offscreen content path.
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 484, 484);
  ASSERT_TRUE(surface != nullptr);

  auto background = ImageLayer::Make();
  background->setImage(MakeImage("resources/apitest/checker_128.png"));
  auto backgroundMatrix = Matrix::MakeScale(3, 3);
  backgroundMatrix.postTranslate(50, 50);
  background->setMatrix(backgroundMatrix);

  auto blurLayer = SolidLayer::Make();
  blurLayer->setColor(Color::FromRGBA(255, 255, 255, 60));
  blurLayer->setWidth(130);
  blurLayer->setHeight(120);
  blurLayer->setMatrix(Matrix::MakeTrans(282, 120));
  blurLayer->setLayerStyles({BackgroundBlurStyle::Make(13, 13)});
  blurLayer->setFilters({BlurFilter::Make(1, 1)});

  auto rootLayer = Layer::Make();
  rootLayer->addChild(background);
  rootLayer->addChild(blurLayer);
  DisplayList displayList;
  displayList.root()->addChild(rootLayer);
  displayList.render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/BackgroundBlurAlignment"));
}

TGFX_TEST(LayerTest, BackgroundColor) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto bgColor = Color::FromRGBA(200, 50, 50);
  const int surfaceWidth = 100;
  const int surfaceHeight = 80;

  // Test 1: DisplayList::render() should paint backgroundColor
  {
    auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
    ASSERT_TRUE(surface != nullptr);
    auto displayList = std::make_unique<DisplayList>();
    displayList->setBackgroundColor(bgColor);
    displayList->render(surface.get());
    context->flushAndSubmit();
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/BackgroundColor_Render"));
  }

  // Test 2: Layer::draw() on root layer should NOT paint backgroundColor
  {
    auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
    ASSERT_TRUE(surface != nullptr);
    auto displayList = std::make_unique<DisplayList>();
    displayList->setBackgroundColor(bgColor);
    // Draw via Layer::draw() directly on root layer — backgroundColor must not appear
    displayList->root()->draw(surface->getCanvas());
    context->flushAndSubmit();
    EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/BackgroundColor_Draw"));
  }
}

TGFX_TEST(LayerTest, TextDuoNoiseTransforms) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto root = Layer::Make();
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));

  auto addText = [&](const std::string& text, const Matrix& matrix) {
    auto textLayer = TextLayer::Make();
    textLayer->setText(text);
    textLayer->setTextColor(Color::FromRGBA(60, 120, 200));
    textLayer->setFont(Font(typeface, 40.f));
    textLayer->setMatrix(matrix);
    textLayer->setAllowsEdgeAntialiasing(false);
    textLayer->setLayerStyles({NoiseStyle::MakeDuo(8.f, 1.f, Color::FromRGBA(200, 50, 50),
                                                   Color::FromRGBA(50, 200, 50), 42.f)});
    root->addChild(textLayer);
  };

  // Text 1: normal (translation only).
  addText("Hello", Matrix::MakeTrans(50.f, 50.f));

  // Text 2: rotated 30 degrees.
  auto rotMatrix = Matrix::MakeTrans(50.f, 150.f);
  rotMatrix.preRotate(30.f, 50.f, 150.f);
  addText("Hello", rotMatrix);

  // Text 3: scaled 1.5x.
  auto scaleMatrix = Matrix::MakeTrans(50.f, 250.f);
  scaleMatrix.preScale(1.5f, 1.5f, 50.f, 250.f);
  addText("Hello", scaleMatrix);

  auto bounds = root->getBounds(nullptr, true);
  auto surface = Surface::Make(context, static_cast<int>(bounds.width() + 100.f),
                               static_cast<int>(bounds.height() + 100.f));
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear();
  surface->getCanvas()->translate(-bounds.left + 50.f, -bounds.top + 50.f);
  root->draw(surface->getCanvas());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/TextDuoNoiseTransforms"));
}

TGFX_TEST(LayerTest, ShapeLayerSmallOvalScaledUp) {
  // Validates the HasSharpCorner device-space threshold via the ShapeLayer -> drawShape
  // -> private drawPath path. A 0.9x0.9 oval under a 100x parent scale must stay round
  // instead of being degenerated into a rectangle by the local-space half-pixel check.
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto displayList = std::make_unique<DisplayList>();

  auto container = Layer::Make();
  auto containerMatrix = Matrix::MakeScale(100.f);
  containerMatrix.postTranslate(50.f, 50.f);
  container->setMatrix(containerMatrix);
  displayList->root()->addChild(container);

  auto shape = ShapeLayer::Make();
  Path circle;
  circle.addOval(Rect::MakeWH(0.9f, 0.9f));
  shape->setPath(circle);
  shape->setFillStyle(ShapeStyle::Make(Color::Red()));

  container->addChild(shape);

  auto surface = Surface::Make(context, 190, 190);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/ShapeLayerSmallOvalScaledUp"));
}

TGFX_TEST(LayerTest, DrawRRectSmallOvalScaledUp) {
  // Validates the Canvas::drawRRect device-space threshold directly, without going through
  // ShapeLayer / drawShape. Guards regressions on the drawRRect entry point itself, which
  // shares the same sub-half-pixel classification logic but is reached by a different path.
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 190, 190);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());
  canvas->translate(50.f, 50.f);
  canvas->scale(100.f, 100.f);

  Paint paint;
  paint.setColor(Color::Red());
  canvas->drawRRect(RRect::MakeOval(Rect::MakeWH(0.9f, 0.9f)), paint);

  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DrawRRectSmallOvalScaledUp"));
}

static const Color YELLOW_COLOR = {1.0f, 1.0f, 0.0f, 1.0f};

enum class ShadowType { Drop, Inner };

static inline std::shared_ptr<LayerStyle> MakeShadow(ShadowType type, float offsetX, float offsetY,
                                                     float blurX, float blurY, const Color& color,
                                                     float spread, bool dropBehindLayer = true) {
  if (type == ShadowType::Drop) {
    auto style = DropShadowStyle::Make(offsetX, offsetY, blurX, blurY, color, dropBehindLayer);
    style->setSpread(spread);
    return style;
  }
  auto style = InnerShadowStyle::Make(offsetX, offsetY, blurX, blurY, color);
  style->setSpread(spread);
  return style;
}

static inline Path MakeStarPath() {
  Path path = {};
  path.moveTo(50, 0);
  path.lineTo(65, 22);
  path.lineTo(100, 30);
  path.lineTo(75, 45);
  path.lineTo(80, 60);
  path.lineTo(50, 50);
  path.lineTo(20, 60);
  path.lineTo(25, 45);
  path.lineTo(0, 30);
  path.lineTo(35, 22);
  path.close();
  return path;
}

static inline void BuildShadowTestLayers(DisplayList& displayList, ShadowType type, float cellW,
                                         float cellH, float gap) {
  const auto lItemXOffset = gap;
  const auto rItemXOffset = gap + cellW / 2.0f;
  auto image = MakeImage("resources/apitest/test_timestretch.png")->makeScaled(100, 60);

  // Case 1: SolidLayer with roundness 10, spread=8
  for (int i = 0; i < 2; ++i) {
    auto layer = SolidLayer::Make();
    layer->setColor(Color::Red());
    layer->setWidth(100);
    layer->setHeight(60);
    layer->setRadiusX(10);
    layer->setRadiusY(10);
    layer->setMatrix(Matrix::MakeTrans(i == 0 ? lItemXOffset : rItemXOffset, gap));
    layer->setLayerStyles({MakeShadow(type, 25, 25, 3, 3, YELLOW_COLOR, i == 0 ? 0.0f : 8.0f)});
    displayList.root()->addChild(layer);
  }

  // Case 2: ImageLayer, spread=8
  for (int i = 0; i < 2; ++i) {
    auto layer = ImageLayer::Make();
    layer->setImage(image);
    layer->setMatrix(Matrix::MakeTrans(cellW + (i == 0 ? lItemXOffset : rItemXOffset), gap));
    layer->setLayerStyles({MakeShadow(type, 25, 25, 3, 3, YELLOW_COLOR, i == 0 ? 0.0f : 8.0f)});
    displayList.root()->addChild(layer);
  }

  // Case 3: ShapeLayer Oval fill, spread=8
  for (int i = 0; i < 2; ++i) {
    auto layer = ShapeLayer::Make();
    Path path = {};
    path.addOval(Rect::MakeXYWH(0.0f, 0.0f, 100.0f, 60.0f));
    layer->setPath(path);
    layer->setFillStyle(ShapeStyle::Make(Color::Red()));
    layer->setMatrix(Matrix::MakeTrans(cellW * 2 + (i == 0 ? lItemXOffset : rItemXOffset), gap));
    layer->setLayerStyles({MakeShadow(type, 25, 25, 3, 3, YELLOW_COLOR, i == 0 ? 0.0f : 8.0f)});
    displayList.root()->addChild(layer);
  }

  // Case 4: ShapeLayer Oval stroke, spread=8
  for (int i = 0; i < 2; ++i) {
    auto layer = ShapeLayer::Make();
    Path path = {};
    path.addOval(Rect::MakeXYWH(0.0f, 0.0f, 100.0f, 60.0f));
    layer->setPath(path);
    layer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 255, 255, 0)));
    layer->setStrokeStyle(ShapeStyle::Make(Color::Blue()));
    layer->setLineWidth(15);
    layer->setMatrix(Matrix::MakeTrans(i == 0 ? lItemXOffset : rItemXOffset, cellH + gap));
    layer->setLayerStyles(
        {MakeShadow(type, 25, 25, 3, 3, Color::Green(), i == 0 ? 0.0f : 8.0f, false)});
    displayList.root()->addChild(layer);
  }

  // Case 5: ShapeLayer star stroke, spread=8
  for (int i = 0; i < 2; ++i) {
    auto layer = ShapeLayer::Make();
    layer->setPath(MakeStarPath());
    layer->setStrokeStyle(ShapeStyle::Make(Color::Blue()));
    layer->setLineWidth(6);
    layer->setMatrix(
        Matrix::MakeTrans(cellW + (i == 0 ? lItemXOffset : rItemXOffset), cellH + gap));
    layer->setLayerStyles({MakeShadow(type, 25, 25, 3, 3, Color::Green(), i == 0 ? 0.0f : 8.0f)});
    displayList.root()->addChild(layer);
  }

  // Case 6: ShapeLayer Rect + ImageShader fill, spread=8
  for (int i = 0; i < 2; ++i) {
    auto layer = ShapeLayer::Make();
    Path path = {};
    path.addRect(Rect::MakeXYWH(0.0f, 0.0f, 100.0f, 60.0f));
    layer->setPath(path);
    auto shader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp);
    layer->setFillStyle(ShapeStyle::Make(shader));
    layer->setMatrix(
        Matrix::MakeTrans(cellW * 2 + (i == 0 ? lItemXOffset : rItemXOffset), cellH + gap));
    layer->setLayerStyles({MakeShadow(type, 25, 25, 3, 3, YELLOW_COLOR, i == 0 ? 0.0f : 8.0f)});
    displayList.root()->addChild(layer);
  }

  // Case 7: ShapeLayer Rect fill + Outside dashed stroke, spread=8
  for (int i = 0; i < 2; ++i) {
    auto layer = ShapeLayer::Make();
    Path path = {};
    path.addRect(Rect::MakeXYWH(0.0f, 0.0f, 100.0f, 60.0f));
    layer->setPath(path);
    layer->setFillStyle(ShapeStyle::Make(Color::Red()));
    layer->setStrokeStyle(ShapeStyle::Make(Color::Blue()));
    layer->setLineWidth(15);
    layer->setStrokeAlign(StrokeAlign::Outside);
    layer->setLineCap(LineCap::Round);
    layer->setLineJoin(LineJoin::Bevel);
    layer->setLineDashPattern({10, 30});
    layer->setMatrix(Matrix::MakeTrans(i == 0 ? lItemXOffset : rItemXOffset, cellH * 2 + gap));
    layer->setLayerStyles({MakeShadow(type, 25, 25, 3, 3, Color::Green(), i == 0 ? 0.0f : 8.0f)});
    displayList.root()->addChild(layer);
  }

  // Case 8: ShapeLayer with an Inside dashed stroke baked into a fill path, spread=8
  for (int i = 0; i < 2; ++i) {
    auto layer = ShapeLayer::Make();
    Path rectPath = {};
    rectPath.addRect(Rect::MakeXYWH(0.0f, 0.0f, 100.0f, 60.0f));
    auto rectShape = Shape::MakeFrom(rectPath);
    const float intervals[] = {10.0f, 20.0f};
    auto dashedShape = Shape::ApplyEffect(rectShape, PathEffect::MakeDash(intervals, 2, 0.0f));
    auto doubled = Stroke(10.0f * 2.0f, LineCap::Butt, LineJoin::Miter);
    auto strokedShape = Shape::ApplyStroke(dashedShape, &doubled);
    auto insideShape = Shape::Merge(strokedShape, rectShape, PathOp::Intersect);
    layer->setPath(insideShape->getPath());
    layer->setFillStyle(ShapeStyle::Make(Color::Blue()));
    layer->setMatrix(
        Matrix::MakeTrans(cellW + (i == 0 ? lItemXOffset : rItemXOffset), cellH * 2 + gap));
    auto child = ShapeLayer::Make();
    child->setPath(rectPath);
    child->setFillStyle(ShapeStyle::Make(Color::Transparent()));
    layer->addChild(child);
    layer->setLayerStyles(
        {MakeShadow(type, 25, 25, 3, 3, Color::Green(), i == 0 ? 0.0f : 8.0f, false)});
    displayList.root()->addChild(layer);
  }

  // Case 9: TextLayer with perspective, spread=8
  {
    auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
    for (int i = 0; i < 2; ++i) {
      auto font = Font(typeface, 40);
      font.setFauxBold(true);
      auto layer = TextLayer::Make();
      layer->setText("TGFX");
      layer->setFont(font);
      layer->setTextColor(Color::Red());
      auto matrix =
          Matrix::MakeTrans(cellW * 2 + (i == 0 ? lItemXOffset : rItemXOffset), cellH * 2 + gap);
      matrix.preConcat(Matrix::MakeAll(1, 0, 0, 0, 1, 0, 0.002f, 0.001f, 1));
      layer->setMatrix(matrix);
      layer->setLayerStyles({MakeShadow(type, 25, 25, 3, 3, YELLOW_COLOR, i == 0 ? 0.0f : 8.0f)});
      displayList.root()->addChild(layer);
    }
  }

  // Case 10: VectorLayer RoundRect + ImagePattern fill, spread=-8
  for (int i = 0; i < 2; ++i) {
    auto vectorLayer = VectorLayer::Make();
    auto group = VectorGroup::Make();
    auto rect = Rectangle::Make();
    rect->setSize({100, 60});
    rect->setPosition({50, 30});
    rect->setRoundness({10, 10, 10, 10});
    auto pattern = ImagePattern::Make(image);
    pattern->setScaleMode(ScaleMode::Zoom);
    auto fill = FillStyle::Make(pattern);
    group->setElements({rect, fill});
    vectorLayer->setContents({group});
    vectorLayer->setMatrix(
        Matrix::MakeTrans(i == 0 ? lItemXOffset : rItemXOffset, cellH * 3 + gap));
    vectorLayer->setLayerStyles(
        {MakeShadow(type, 25, 25, 3, 3, YELLOW_COLOR, i == 0 ? 0.0f : -8.0f)});
    displayList.root()->addChild(vectorLayer);
  }

  // Case 11: VectorLayer Ellipse + Center stroke 15px, spread=-10 (stroke collapse)
  for (int i = 0; i < 2; ++i) {
    auto vectorLayer = VectorLayer::Make();
    auto group = VectorGroup::Make();
    auto ellipse = Ellipse::Make();
    ellipse->setPosition({50, 30});
    ellipse->setSize({100, 60});
    auto stroke = StrokeStyle::Make(SolidColor::Make(Color::Blue()));
    stroke->setStrokeWidth(15);
    group->setElements({ellipse, stroke});
    vectorLayer->setContents({group});
    vectorLayer->setMatrix(
        Matrix::MakeTrans(cellW + (i == 0 ? lItemXOffset : rItemXOffset), cellH * 3 + gap));
    vectorLayer->setLayerStyles(
        {MakeShadow(type, 25, 25, 3, 3, Color::Green(), i == 0 ? 0.0f : -10.0f)});
    displayList.root()->addChild(vectorLayer);
  }

  // Case 12: VectorLayer star fill, spread=8
  for (int i = 0; i < 2; ++i) {
    auto vectorLayer = VectorLayer::Make();
    auto group = VectorGroup::Make();
    auto star = ShapePath::Make();
    star->setPath(MakeStarPath());
    auto fill = FillStyle::Make(SolidColor::Make(Color::Red()));
    group->setElements({star, fill});
    vectorLayer->setContents({group});
    vectorLayer->setMatrix(
        Matrix::MakeTrans(cellW * 2 + (i == 0 ? lItemXOffset : rItemXOffset), cellH * 3 + gap));
    vectorLayer->setLayerStyles(
        {MakeShadow(type, 25, 25, 3, 3, Color::Green(), i == 0 ? 0.0f : 8.0f)});
    displayList.root()->addChild(vectorLayer);
  }

  // Case 13: VectorLayer Rect+Ellipse + Inside stroke 8px, spread=5
  for (int i = 0; i < 2; ++i) {
    auto vectorLayer = VectorLayer::Make();
    auto group = VectorGroup::Make();
    auto rect = Rectangle::Make();
    rect->setPosition({25, 30});
    rect->setSize({50, 60});
    auto ellipse = Ellipse::Make();
    ellipse->setPosition({75, 30});
    ellipse->setSize({50, 60});
    auto stroke = StrokeStyle::Make(SolidColor::Make(Color::Blue()));
    stroke->setStrokeWidth(8);
    stroke->setStrokeAlign(StrokeAlign::Inside);
    group->setElements({rect, ellipse, stroke});
    vectorLayer->setContents({group});
    vectorLayer->setMatrix(
        Matrix::MakeTrans(i == 0 ? lItemXOffset : rItemXOffset, cellH * 4 + gap));
    vectorLayer->setLayerStyles(
        {MakeShadow(type, 25, 25, 3, 3, Color::Green(), i == 0 ? 0.0f : 5.0f)});
    displayList.root()->addChild(vectorLayer);
  }

  // Case 14: VectorLayer Ellipse + Outside dashed stroke 8px + SolidLayer child, spread=8
  for (int i = 0; i < 2; ++i) {
    auto vectorLayer = VectorLayer::Make();
    auto group = VectorGroup::Make();
    auto ellipse = Ellipse::Make();
    ellipse->setPosition({50, 30});
    ellipse->setSize({100, 60});
    auto stroke = StrokeStyle::Make(SolidColor::Make(Color::Blue()));
    stroke->setStrokeWidth(8);
    stroke->setStrokeAlign(StrokeAlign::Outside);
    stroke->setLineCap(LineCap::Round);
    stroke->setDashes({10, 25});
    group->setElements({ellipse, stroke});
    vectorLayer->setContents({group});
    vectorLayer->setMatrix(
        Matrix::MakeTrans(cellW + (i == 0 ? lItemXOffset : rItemXOffset), cellH * 4 + gap));
    auto child = SolidLayer::Make();
    child->setWidth(30);
    child->setHeight(20);
    child->setColor(Color::Red());
    child->setMatrix(Matrix::MakeTrans(35, 20));
    vectorLayer->addChild(child);
    vectorLayer->setLayerStyles(
        {MakeShadow(type, 25, 25, 3, 3, Color::Green(), i == 0 ? 0.0f : 8.0f)});
    displayList.root()->addChild(vectorLayer);
  }

  // Case 15: VectorLayer Rect fill + Outside dashed stroke, spread=8
  for (int i = 0; i < 2; ++i) {
    auto vectorLayer = VectorLayer::Make();
    auto group = VectorGroup::Make();
    auto rect = Rectangle::Make();
    rect->setSize({100, 60});
    rect->setPosition({50, 30});
    auto fill = FillStyle::Make(SolidColor::Make(Color::Red()));
    auto stroke = StrokeStyle::Make(SolidColor::Make(Color::Blue()));
    stroke->setStrokeWidth(15);
    stroke->setStrokeAlign(StrokeAlign::Outside);
    stroke->setLineCap(LineCap::Round);
    stroke->setLineJoin(LineJoin::Bevel);
    stroke->setDashes({10, 30});
    group->setElements({rect, fill, stroke});
    vectorLayer->setContents({group});
    vectorLayer->setMatrix(
        Matrix::MakeTrans(cellW * 2 + (i == 0 ? lItemXOffset : rItemXOffset), cellH * 4 + gap));
    vectorLayer->setLayerStyles(
        {MakeShadow(type, 25, 25, 3, 3, Color::Green(), i == 0 ? 0.0f : 8.0f)});
    displayList.root()->addChild(vectorLayer);
  }

  // Case 16: VectorLayer Ellipse + Inside stroke 15px with compound transform, spread=-5
  for (int i = 0; i < 2; ++i) {
    auto vectorLayer = VectorLayer::Make();
    auto innerGroup = VectorGroup::Make();
    auto ellipse = Ellipse::Make();
    ellipse->setPosition({50, 30});
    ellipse->setSize({100, 60});
    auto stroke = StrokeStyle::Make(SolidColor::Make(Color::Blue()));
    stroke->setStrokeWidth(15);
    stroke->setStrokeAlign(StrokeAlign::Inside);
    auto fill = FillStyle::Make(SolidColor::Make(Color::Transparent()));
    innerGroup->setElements({ellipse, stroke, fill});
    innerGroup->setScale({1.5f, 1.5f});
    innerGroup->setRotation(-30);
    auto group = VectorGroup::Make();
    group->setElements({innerGroup});
    vectorLayer->setContents({group});
    auto matrix = Matrix::MakeTrans(i == 0 ? lItemXOffset : rItemXOffset, cellH * 5 + gap);
    matrix.preConcat(Matrix::MakeScale(0.8f));
    matrix.preConcat(Matrix::MakeRotate(15));
    matrix.preConcat(Matrix::MakeTrans(-10, 15));
    vectorLayer->setMatrix(matrix);
    vectorLayer->setLayerStyles(
        {MakeShadow(type, 25, 25, 3, 3, Color::Green(), i == 0 ? 0.0f : -5.0f, false)});
    displayList.root()->addChild(vectorLayer);
  }

  // Case 17: VectorLayer Ellipse + Outside dashed stroke 15px with transparent-fill child, spread=6
  for (int i = 0; i < 2; ++i) {
    auto vectorLayer = VectorLayer::Make();
    auto group = VectorGroup::Make();
    auto ellipse = Ellipse::Make();
    ellipse->setPosition({50, 30});
    ellipse->setSize({100, 60});
    auto stroke = StrokeStyle::Make(SolidColor::Make(Color::Blue()));
    stroke->setStrokeWidth(15);
    stroke->setStrokeAlign(StrokeAlign::Outside);
    stroke->setLineCap(LineCap::Butt);
    stroke->setLineJoin(LineJoin::Miter);
    stroke->setDashes({10, 20});
    group->setElements({ellipse, stroke});
    vectorLayer->setContents({group});
    vectorLayer->setMatrix(
        Matrix::MakeTrans(cellW + (i == 0 ? lItemXOffset : rItemXOffset), cellH * 5 + gap));
    auto child = VectorLayer::Make();
    auto childGroup = VectorGroup::Make();
    auto childEllipse = Ellipse::Make();
    childEllipse->setPosition({50, 30});
    childEllipse->setSize({100, 60});
    auto childFill = FillStyle::Make(SolidColor::Make(Color::Transparent()));
    childGroup->setElements({childEllipse, childFill});
    child->setContents({childGroup});
    vectorLayer->addChild(child);
    vectorLayer->setLayerStyles(
        {MakeShadow(type, 25, 25, 3, 3, Color::Green(), i == 0 ? 0.0f : 6.0f, false)});
    displayList.root()->addChild(vectorLayer);
  }
}

TGFX_TEST(LayerTest, DropShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  constexpr float cellW = 400.0f;
  constexpr float cellH = 120.0f;
  constexpr float gap = 30.0f;
  auto surface =
      Surface::Make(context, static_cast<int>(cellW * 3 + gap), static_cast<int>(cellH * 6 + gap));
  auto displayList = std::make_unique<DisplayList>();
  BuildShadowTestLayers(*displayList, ShadowType::Drop, cellW, cellH, gap);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/DropShadow"));
}

TGFX_TEST(LayerTest, InnerShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  constexpr float cellW = 400.0f;
  constexpr float cellH = 120.0f;
  constexpr float gap = 30.0f;
  auto surface =
      Surface::Make(context, static_cast<int>(cellW * 3 + gap), static_cast<int>(cellH * 6 + gap));
  auto displayList = std::make_unique<DisplayList>();
  BuildShadowTestLayers(*displayList, ShadowType::Inner, cellW, cellH, gap);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/InnerShadow"));
}

// Creates a single glass cell: background image + colored rect + ellipse glass panel at (x, y).
static void AddGlassCell(Layer* root, std::shared_ptr<Image> bgImage, float x, float y,
                         float cellSize, float refraction, float depth, float frost,
                         float dispersion, float splay, float lightAngle, float lightIntensity,
                         float glassW = -1.0f, float glassH = -1.0f, float radiusX = -1.0f,
                         float radiusY = -1.0f, Color rectColor = Color::FromRGBA(0, 100, 255, 255),
                         float layerScale = 1.0f) {
  auto container = Layer::Make();
  container->setMatrix(Matrix::MakeTrans(x, y));

  // Background image scaled to fill the cell
  if (bgImage) {
    auto imgLayer = ImageLayer::Make();
    imgLayer->setImage(bgImage);
    auto scale = cellSize / static_cast<float>(std::max(bgImage->width(), bgImage->height()));
    imgLayer->setMatrix(Matrix::MakeScale(scale, scale));
    container->addChild(imgLayer);
  }

  // Colored rectangles for refraction contrast
  auto blueRect = ShapeLayer::Make();
  Path bluePath = {};
  bluePath.addRect(
      Rect::MakeXYWH(cellSize * 0.15f, cellSize * 0.15f, cellSize * 0.35f, cellSize * 0.35f));
  blueRect->setPath(bluePath);
  blueRect->setFillStyle(ShapeStyle::Make(rectColor));
  container->addChild(blueRect);

  auto greenCircle = ShapeLayer::Make();
  Path greenPath = {};
  greenPath.addOval(
      Rect::MakeXYWH(cellSize * 0.45f, cellSize * 0.45f, cellSize * 0.4f, cellSize * 0.4f));
  greenCircle->setPath(greenPath);
  greenCircle->setFillStyle(ShapeStyle::Make(Color::FromRGBA(50, 200, 80, 200)));
  container->addChild(greenCircle);

  // Glass panel shape. Default is a circle (ellipse with equal width/height); callers can
  // override to render a rounded rect or an ellipse with unequal dimensions, which exercise
  // the analytical SDF refraction path.
  float defaultSize = cellSize - 20;
  // width/height use > 0 (zero size is invalid), but radius uses >= 0 (zero radius is a valid
  // sharp rect).
  float gw = glassW > 0.0f ? glassW : defaultSize;
  float gh = glassH > 0.0f ? glassH : defaultSize;
  float rx = radiusX >= 0.0f ? radiusX : gw * 0.5f;
  float ry = radiusY >= 0.0f ? radiusY : gh * 0.5f;
  auto glassLayer = SolidLayer::Make();
  glassLayer->setColor(Color::FromRGBA(255, 255, 255, 128));
  glassLayer->setWidth(gw);
  glassLayer->setHeight(gh);
  glassLayer->setRadiusX(rx);
  glassLayer->setRadiusY(ry);
  // Draw the panel smaller about the cell centre; contentScale then becomes layerScale, which
  // widens the edge band to max(1.5, 1 / layerScale) layer pixels.
  float offsetX = (cellSize - gw) * 0.5f + (1.0f - layerScale) * gw * 0.5f;
  float offsetY = (cellSize - gh) * 0.5f + (1.0f - layerScale) * gh * 0.5f;
  glassLayer->setMatrix(Matrix::MakeTrans(offsetX, offsetY) * Matrix::MakeScale(layerScale));
  auto style =
      GlassStyle::Make(refraction, depth, frost, dispersion, splay, lightAngle, lightIntensity);
  glassLayer->setLayerStyles({style});
  container->addChild(glassLayer);

  root->addChild(container);
}

static void RunGlassStyleTest(const std::string& keySuffix, float zoomScale = 1.0f,
                              float glassW = -1.0f, float glassH = -1.0f, float radiusX = -1.0f,
                              float radiusY = -1.0f) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  constexpr float cellSize = 200;
  constexpr float gap = 10;
  constexpr int cols = 3;
  constexpr int rows = 7;
  int surfaceW = static_cast<int>(cols * (cellSize + gap) + gap);
  int surfaceH = static_cast<int>(rows * (cellSize + gap) + gap);
  auto surface = Surface::Make(context, surfaceW, surfaceH);
  auto displayList = std::make_unique<DisplayList>();
  if (zoomScale != 1.0f) {
    displayList->setZoomScale(zoomScale);
  }
  auto bgImage = MakeImage("resources/apitest/checker_128.png");

  float defRef = 50, defDepth = 70, defFrost = 0, defDisp = 0;
  float defSplay = 50, defAngle = 135, defIntensity = 50;

  float refractionVals[] = {10, 50, 90};
  float depthVals[] = {5, 25, 60};
  float frostVals[] = {0, 30, 80};
  float dispVals[] = {0, 40, 90};
  float splayVals[] = {0, 50, 100};
  float angleVals[] = {0, 135, 270};
  float intensityVals[] = {0, 50, 100};

  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      float cx = gap + static_cast<float>(c) * (cellSize + gap);
      float cy = gap + static_cast<float>(r) * (cellSize + gap);
      float ref = defRef, depth = defDepth, frost = defFrost, disp = defDisp;
      float splay = defSplay, angle = defAngle, intensity = defIntensity;
      switch (r) {
        case 0:
          ref = refractionVals[c];
          break;
        case 1:
          depth = depthVals[c];
          break;
        case 2:
          frost = frostVals[c];
          break;
        case 3:
          disp = dispVals[c];
          break;
        case 4:
          splay = splayVals[c];
          break;
        case 5:
          angle = angleVals[c];
          break;
        case 6:
          intensity = intensityVals[c];
          break;
      }
      AddGlassCell(displayList->root(), bgImage, cx, cy, cellSize, ref, depth, frost, disp, splay,
                   angle, intensity, glassW, glassH, radiusX, radiusY);
    }
  }

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/GlassStyle" + keySuffix));
}
TGFX_TEST_PRIVATE(LayerTest, GlassStyleStrokeContentBounds) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  std::vector<int> widths = {};
  std::vector<float> offsets = {};

  for (auto strokeAlign : {StrokeAlign::Center, StrokeAlign::Inside, StrokeAlign::Outside}) {
    auto vectorLayer = VectorLayer::Make();
    auto rectangle = Rectangle::Make();
    rectangle->setPosition({100, 100});
    rectangle->setSize({100, 100});
    rectangle->setRoundness({20, 20, 20, 20});
    auto fill = FillStyle::Make(SolidColor::Make(Color::White()));
    auto stroke = StrokeStyle::Make(SolidColor::Make(Color::Blue()));
    stroke->setStrokeWidth(10);
    stroke->setStrokeAlign(strokeAlign);
    vectorLayer->setContents({rectangle, fill, stroke});
    vectorLayer->setLayerStyles({GlassStyle::Make(80, 50, 0, 50, 0, 135, 50)});
    DrawArgs drawArgs(context);
    // The recording Glass samples is the layer's geometry: its bucket records no shared content.
    TGFX_PRIVATE_ACCESS(auto source = vectorLayer->getLayerStyleSource(drawArgs, Matrix::I());
                        ASSERT_TRUE(source != nullptr); ASSERT_TRUE(source->groups[0] != nullptr);
                        ASSERT_TRUE(source->groups[0]->geometry != nullptr);
                        ASSERT_TRUE(source->groups[0]->geometry->image() != nullptr);
                        widths.push_back(source->groups[0]->geometry->image()->width());
                        offsets.push_back(source->groups[0]->geometry->imageOffset().x);)
  }
  ASSERT_EQ(widths.size(), 3u);
  EXPECT_EQ(widths[1], 100);
  EXPECT_LT(offsets[0], offsets[1]);
  EXPECT_GT(widths[0], widths[1]);
  EXPECT_LT(offsets[2], offsets[0]);
  EXPECT_GT(widths[2], widths[0]);
}

TGFX_TEST_PRIVATE(LayerTest, ShapeLayerContentShapeExactness) {
  auto layer = ShapeLayer::Make();
  Path path = {};
  path.addRRect(RRect::MakeRectXY(Rect::MakeXYWH(50, 50, 100, 100), 20, 20));
  layer->setPath(path);
  layer->setFillStyle(ShapeStyle::Make(Color::White()));
  layer->setStrokeStyle(ShapeStyle::Make(Color::Blue()));
  TGFX_PRIVATE_ACCESS(auto singleStroke = layer->onGetContentShape();
                      ASSERT_TRUE(singleStroke.has_value());
                      EXPECT_EQ(singleStroke->type, StyledShapeType::FillStroke);
                      EXPECT_TRUE(singleStroke->shape != nullptr);)

  layer->addStrokeStyle(ShapeStyle::Make(Color::Red()));
  TGFX_PRIVATE_ACCESS(auto multipleStrokes = layer->onGetContentShape();
                      ASSERT_TRUE(multipleStrokes.has_value());
                      // Stacked stroke styles share the layer's single width and alignment, so the
                      // contour stays exact no matter how many are stacked.
                      EXPECT_EQ(multipleStrokes->type, StyledShapeType::FillStroke);
                      EXPECT_TRUE(multipleStrokes->shape != nullptr);)

  auto multipleFills = ShapeLayer::Make();
  multipleFills->setPath(path);
  multipleFills->setFillStyles({ShapeStyle::Make(Color::White()), ShapeStyle::Make(Color::Blue())});
  TGFX_PRIVATE_ACCESS(auto multipleFillShape = multipleFills->onGetContentShape();
                      ASSERT_TRUE(multipleFillShape.has_value());
                      EXPECT_EQ(multipleFillShape->type, StyledShapeType::Fill);
                      EXPECT_TRUE(multipleFillShape->shape != nullptr);)

  auto strokeOnly = ShapeLayer::Make();
  strokeOnly->setPath(path);
  strokeOnly->addStrokeStyle(ShapeStyle::Make(Color::Blue()));
  strokeOnly->addStrokeStyle(ShapeStyle::Make(Color::Red()));
  TGFX_PRIVATE_ACCESS(auto strokeOnlyShape = strokeOnly->onGetContentShape();
                      ASSERT_TRUE(strokeOnlyShape.has_value());
                      EXPECT_EQ(strokeOnlyShape->type, StyledShapeType::Stroke);
                      EXPECT_TRUE(strokeOnlyShape->shape != nullptr);)
}

static std::optional<uint32_t> RenderSpreadShadowPixel(Context* context,
                                                       const std::shared_ptr<Layer>& layer, int x,
                                                       int y) {
  auto surface = Surface::Make(context, 200, 200);
  if (surface == nullptr) {
    return std::nullopt;
  }
  auto displayList = std::make_unique<DisplayList>();
  displayList->root()->addChild(layer);
  displayList->render(surface.get());
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  uint32_t pixel = 0;
  if (!surface->readPixels(info, &pixel, x, y)) {
    return std::nullopt;
  }
  return pixel;
}

static std::shared_ptr<VectorLayer> MakeMultiGeometryVectorLayer() {
  auto vectorLayer = VectorLayer::Make();
  auto group = VectorGroup::Make();
  auto rect = Rectangle::Make();
  rect->setPosition({100, 100});
  rect->setSize({100, 100});
  auto ellipse = Ellipse::Make();
  ellipse->setPosition({100, 100});
  ellipse->setSize({80, 80});
  auto fill = FillStyle::Make(SolidColor::Make(Color::FromRGBA(255, 255, 255, 255)));
  group->setElements({rect, ellipse, fill});
  vectorLayer->setContents({group});
  return vectorLayer;
}

// Multiple geometries cannot produce a single exact outline: the spread falls back to the
// content image's bounds, so it still applies — the shadow expands by the spread from the
// bounds rect rather than hugging the content.
TGFX_TEST(LayerTest, DropShadowSpreadWithoutExactShape) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // Both geometries are centered at (100, 100); the rect spans [50, 150], so the content bounds
  // end at 150. The shadow has no blur and a Y offset of 15, and a spread of 10 expands the
  // bounds footprint to 160, so the shadow reaches 175.
  auto layer = MakeMultiGeometryVectorLayer();
  auto shadowStyle = DropShadowStyle::Make(0, 15, 0, 0, Color::Black());
  shadowStyle->setSpread(10);
  layer->setLayerStyles({shadowStyle});
  // Within the spread-expanded footprint (150 + 10 + 15).
  auto spreadPixel = RenderSpreadShadowPixel(context, layer, 100, 170);
  ASSERT_TRUE(spreadPixel.has_value());
  EXPECT_NE(*spreadPixel, 0u) << "spread was not applied from the content bounds";
  // Beyond the spread-expanded footprint the shadow must be absent.
  auto outsidePixel = RenderSpreadShadowPixel(context, layer, 100, 180);
  ASSERT_TRUE(outsidePixel.has_value());
  EXPECT_EQ(*outsidePixel, 0u);
}
TGFX_TEST_PRIVATE(LayerTest, GlassStyleDeclaresGeometryContentSource) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto layer = SolidLayer::Make();
  layer->setWidth(100);
  layer->setHeight(80);
  auto glassStyle = GlassStyle::Make(80, 50, 0, 50, 0, 0, 0);
  layer->setLayerStyles({glassStyle});

  // Glass declares where its content comes from instead of flagging a source: it draws from the
  // layer's geometry and only needs the background on top of that.
  EXPECT_EQ(glassStyle->contentSource(), LayerStyleContentSource::Geometry);
  EXPECT_EQ(glassStyle->extraSourceType(),
            static_cast<uint32_t>(LayerStyleExtraSourceType::Background));

  DrawArgs drawArgs(context);
  TGFX_PRIVATE_ACCESS(auto source = layer->getLayerStyleSource(drawArgs, Matrix::I());
                      ASSERT_TRUE(source != nullptr); ASSERT_TRUE(source->groups[0] != nullptr);
                      ASSERT_TRUE(source->groups[0]->geometry != nullptr);
                      EXPECT_TRUE(source->groups[0]->geometry->image() != nullptr);
                      EXPECT_TRUE(source->groups[0]->geometry->shape().has_value())
                      << "a layered rect has an exact geometry shape, so Glass takes the SDF "
                         "path";
                      EXPECT_TRUE(source->groups[0]->contour == nullptr);
                      // Nothing in a Geometry-only bucket reads the shared content, so recording it
                      // would rasterize the whole subtree for nobody.
                      EXPECT_TRUE(source->groups[0]->content == nullptr)
                      << "a Geometry-only bucket must not record the shared content";)

  // The contrast that keeps the assertion above honest: a bucket holding a style that draws from the
  // shared content still records it, so the skip above is a real classification and not a blanket
  // skip.
  auto mixedLayer = SolidLayer::Make();
  mixedLayer->setWidth(100);
  mixedLayer->setHeight(80);
  mixedLayer->setLayerStyles({GlassStyle::Make(80, 50, 0, 50, 0, 0, 0),
                              DropShadowStyle::Make(0, 15, 0, 0, Color::Black())});
  TGFX_PRIVATE_ACCESS(auto mixedSource = mixedLayer->getLayerStyleSource(drawArgs, Matrix::I());
                      ASSERT_TRUE(mixedSource != nullptr);
                      ASSERT_TRUE(mixedSource->groups[0] != nullptr);
                      EXPECT_TRUE(mixedSource->groups[0]->geometry != nullptr);
                      EXPECT_TRUE(mixedSource->groups[0]->content != nullptr)
                      << "a non-Geometry style in the same bucket still needs the shared content";)
}

// The geometry raster and shape of a layer never depend on the excludeChildEffects bucket: the
// recording reads the layer's own content, its own paint, the edge-antialiasing flag and the
// destination color space only. One recording therefore serves both buckets, and the shared image
// pointer is the observable proof -- recording per bucket again would hand each group its own image.
TGFX_TEST_PRIVATE(LayerTest, LayerStyleGeometryRecordedOnceForBothBuckets) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto layer = VectorLayer::Make();
  auto shape = Rectangle::Make();
  shape->setPosition({50.0f, 40.0f});
  shape->setSize({100.0f, 80.0f});
  shape->setRoundness(12.0f);
  auto group = VectorGroup::Make();
  group->setElements({shape, FillStyle::Make(SolidColor::Make(Color::White()))});
  layer->setContents({group});
  // Two Geometry-sourced styles landing in the two different buckets, so both buckets need the
  // geometry and the recording above must serve both.
  auto seesChildren = GlassStyle::Make(0, 50, 100, 0, 0, 0, 0);
  seesChildren->setExcludeChildEffects(false);
  auto seesOwnOnly = GlassStyle::Make(0, 50, 100, 0, 0, 0, 0);
  seesOwnOnly->setExcludeChildEffects(true);
  layer->setLayerStyles({seesChildren, seesOwnOnly});

  DrawArgs drawArgs(context);
  TGFX_PRIVATE_ACCESS(
      auto source = layer->getLayerStyleSource(drawArgs, Matrix::I());
      ASSERT_TRUE(source != nullptr);
      ASSERT_TRUE(source->groups[0] != nullptr) << "the excludeChildEffects=false bucket";
      ASSERT_TRUE(source->groups[1] != nullptr) << "the excludeChildEffects=true bucket";
      ASSERT_TRUE(source->groups[0]->geometry != nullptr);
      ASSERT_TRUE(source->groups[1]->geometry != nullptr);
      EXPECT_EQ(source->groups[0]->geometry->image(), source->groups[1]->geometry->image())
      << "both buckets must share one geometry recording";
      EXPECT_EQ(source->groups[0]->geometry->imageOffset(),
                source->groups[1]->geometry->imageOffset());
      EXPECT_TRUE(source->groups[0]->geometry->shape().has_value());
      EXPECT_TRUE(source->groups[1]->geometry->shape().has_value());)
}

// The contrast that keeps the hoisting honest: a layer whose geometry lives in its children records
// no geometry, and the lazy contour fallback that answers for it does read
// drawArgs.excludeEffects. That fallback must therefore stay per bucket, so the two groups hold two
// distinct images and hoisting it too would silently collapse them into one.
TGFX_TEST_PRIVATE(LayerTest, LayerStyleGeometryFallbackStaysPerBucket) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // The parent paints nothing itself: every painter lives in a child layer.
  auto parent = Layer::Make();
  auto child = VectorLayer::Make();
  auto shape = Rectangle::Make();
  shape->setPosition({50.0f, 40.0f});
  shape->setSize({100.0f, 80.0f});
  shape->setRoundness(12.0f);
  auto group = VectorGroup::Make();
  group->setElements({shape, FillStyle::Make(SolidColor::Make(Color::White()))});
  child->setContents({group});
  parent->addChild(child);
  auto seesChildren = GlassStyle::Make(0, 50, 100, 0, 0, 0, 0);
  seesChildren->setExcludeChildEffects(false);
  auto seesOwnOnly = GlassStyle::Make(0, 50, 100, 0, 0, 0, 0);
  seesOwnOnly->setExcludeChildEffects(true);
  parent->setLayerStyles({seesChildren, seesOwnOnly});

  DrawArgs drawArgs(context);
  TGFX_PRIVATE_ACCESS(
      auto source = parent->getLayerStyleSource(drawArgs, Matrix::I());
      ASSERT_TRUE(source != nullptr); ASSERT_TRUE(source->groups[0] != nullptr);
      ASSERT_TRUE(source->groups[1] != nullptr);
      EXPECT_TRUE(source->groups[0]->geometry == nullptr)
      << "a layer painting nothing must not record geometry, otherwise the fallback below is "
         "never reached";
      EXPECT_TRUE(source->groups[1]->geometry == nullptr);
      ASSERT_TRUE(source->groups[0]->contour != nullptr);
      ASSERT_TRUE(source->groups[1]->contour != nullptr);
      ASSERT_TRUE(source->groups[0]->contour->image() != nullptr);
      ASSERT_TRUE(source->groups[1]->contour->image() != nullptr);
      EXPECT_NE(source->groups[0]->contour->image().get(),
                source->groups[1]->contour->image().get())
      << "the lazy fallback reads excludeChildEffects, so it must stay recorded per bucket; one "
         "shared image here means the fallback was hoisted along with the geometry";)
}

// ===== The layer's geometry: coverage asserted on rendered pixels =====
//
// A Glass style takes its coverage shape from the layer's geometry, so the cases below are
// written the way the rest of this file is: set an input, render the layer tree over a checker
// backdrop and assert on the rendered pixels. Baseline::Compare is deliberately not used, because no
// stored reference images exist for these cases and adding one would require a baseline version
// bump. Internal recordings are asserted only where a rendered difference cannot attribute a
// failure, and every such assertion says why it is there.

// A fill painter that paints nothing while still capturing its geometry: style alpha 0 over a color
// alpha of 1. The contour keeps it, so the geometry still belongs to the layer's geometry.
static std::shared_ptr<FillStyle> MakeZeroAlphaFill() {
  auto fill = FillStyle::Make(SolidColor::Make(Color::White()));
  fill->setAlpha(0.0f);
  return fill;
}

// One region = one geometry plus its own fill, so geometry/painter pairing stays per region.
static std::shared_ptr<VectorGroup> MakeFilledRegion(const std::shared_ptr<VectorElement>& geometry,
                                                     const std::shared_ptr<FillStyle>& fill) {
  auto group = VectorGroup::Make();
  group->setElements({geometry, fill});
  return group;
}

// Renders `panel` on top of a high-frequency checker backdrop so any refraction shows up as a large
// pixel difference.
static std::shared_ptr<Surface> RenderPanelOverChecker(Context* context,
                                                       const std::shared_ptr<Layer>& panel) {
  constexpr int surfaceSize = 200;
  auto surface = Surface::Make(context, surfaceSize, surfaceSize);
  if (surface == nullptr) {
    return nullptr;
  }
  auto displayList = std::make_unique<DisplayList>();
  auto background = ImageLayer::Make();
  background->setImage(MakeImage("resources/apitest/checker_128.png"));
  background->setMatrix(Matrix::MakeScale(2));
  displayList->root()->addChild(background);
  displayList->root()->addChild(panel);
  displayList->render(surface.get());
  return surface;
}

// Counts the pixels inside `region` where any color channel differs by more than `tolerance`.
// Returns -1 when the two surfaces cannot be compared.
static int CountDifferingPixels(Surface* first, Surface* second, const Rect& region,
                                int tolerance) {
  auto width = first->width();
  auto height = first->height();
  if (width != second->width() || height != second->height()) {
    return -1;
  }
  auto info = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Premultiplied);
  std::vector<uint32_t> firstPixels(static_cast<size_t>(width) * static_cast<size_t>(height));
  std::vector<uint32_t> secondPixels(firstPixels.size());
  if (!first->readPixels(info, firstPixels.data()) ||
      !second->readPixels(info, secondPixels.data())) {
    return -1;
  }
  auto left = std::max(0, static_cast<int>(region.left));
  auto top = std::max(0, static_cast<int>(region.top));
  auto right = std::min(width, static_cast<int>(region.right));
  auto bottom = std::min(height, static_cast<int>(region.bottom));
  int count = 0;
  for (int y = top; y < bottom; y++) {
    for (int x = left; x < right; x++) {
      auto index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
      int maxDiff = 0;
      for (int shift = 0; shift < 32; shift += 8) {
        auto firstChannel = static_cast<int>((firstPixels[index] >> shift) & 0xFF);
        auto secondChannel = static_cast<int>((secondPixels[index] >> shift) & 0xFF);
        maxDiff = std::max(maxDiff, firstChannel > secondChannel ? firstChannel - secondChannel
                                                                 : secondChannel - firstChannel);
      }
      if (maxDiff > tolerance) {
        count++;
      }
    }
  }
  return count;
}

constexpr float GlassPanelLeft = 50.0f;
constexpr float GlassPanelTop = 50.0f;
constexpr float GlassPanelWidth = 100.0f;
constexpr float GlassPanelHeight = 80.0f;
constexpr float GlassPanelSurfaceExtent = 200.0f;
constexpr int GlassPanelInteriorPixels =
    static_cast<int>(GlassPanelWidth - 2.0f) * static_cast<int>(GlassPanelHeight - 2.0f);

static Rect GlassPanelBounds() {
  return Rect::MakeXYWH(GlassPanelLeft, GlassPanelTop, GlassPanelWidth, GlassPanelHeight);
}

// The same panel in the layer's own local space: MakePanel places the layer at the
// standard offset, so a region spec carries local bounds while every ROI is in surface coordinates.
static Rect GlassPanelLocalBounds() {
  return Rect::MakeWH(GlassPanelWidth, GlassPanelHeight);
}

// ===== Shared region fixtures =====

// Which painter a region carries over its geometry.
//   None        - no painter at all: the geometry joins the layer's geometry as bare geometry
//                 and contributes nothing to the shared content or contour.
//   Transparent - a retained 0%-alpha fill: geometry only, but it stays in the shared contour.
//   TransparentColor - a fully transparent solid color (alpha 0 in the color source), which the
//                      content-shape derivation skips.
//   Opaque      - a visible fill.
//   Gradient    - a shader fill that fades to fully transparent on its right half.
enum class RegionFillKind { None, Transparent, TransparentColor, Opaque, Gradient };

// One rectangle region of a panel plus the overrides the special cases need.
struct RegionSpec {
  Rect bounds = {};
  RegionFillKind fill = RegionFillKind::None;
  float roundness = 0.0f;                          // 0 = sharp rectangle
  std::shared_ptr<VectorElement> shape = nullptr;  // overrides bounds/roundness when set
  float strokeWidth = 0.0f;                        // 0 = no stroke
  StrokeAlign strokeAlign = StrokeAlign::Outside;
};

// One region: the geometry plus its painter. This and MakePanel are the two shared
// builders every region case routes through -- the same region rectangle used to be rebuilt inline
// in a dozen places, which made it hard to tell whether two cases differed in the fixture or in the
// behaviour.
static std::shared_ptr<VectorGroup> MakeRegion(const RegionSpec& spec) {
  std::shared_ptr<VectorElement> geometry = spec.shape;
  if (geometry == nullptr) {
    auto rect = Rectangle::Make();
    rect->setPosition({spec.bounds.centerX(), spec.bounds.centerY()});
    rect->setSize({spec.bounds.width(), spec.bounds.height()});
    rect->setRoundness(spec.roundness);
    geometry = rect;
  }
  std::vector<std::shared_ptr<VectorElement>> elements = {geometry};
  switch (spec.fill) {
    case RegionFillKind::None:
      break;
    case RegionFillKind::Transparent:
      elements.push_back(MakeZeroAlphaFill());
      break;
    case RegionFillKind::TransparentColor:
      elements.push_back(FillStyle::Make(SolidColor::Make(Color::Transparent())));
      break;
    case RegionFillKind::Opaque:
      elements.push_back(FillStyle::Make(SolidColor::Make(Color::White())));
      break;
    case RegionFillKind::Gradient: {
      // Fully transparent from its midpoint on, so the right half contributes no visible pixel
      // while still being part of the geometry. The points are in the gradient's default
      // normalized space, which maps to this geometry's own bounding box.
      auto gradient = Gradient::MakeLinear(
          {0.0f, 0.5f}, {1.0f, 0.5f}, {Color::White(), Color::Transparent(), Color::Transparent()},
          {0.0f, 0.5f, 1.0f});
      elements.push_back(FillStyle::Make(std::move(gradient)));
      break;
    }
  }
  if (spec.strokeWidth > 0.0f) {
    // Translucent, so the Glass drawn under the band stays observable in a rendered comparison.
    auto stroke = StrokeStyle::Make(SolidColor::Make(Color::Blue()));
    stroke->setAlpha(0.4f);
    stroke->setStrokeWidth(spec.strokeWidth);
    stroke->setStrokeAlign(spec.strokeAlign);
    elements.push_back(stroke);
  }
  auto group = VectorGroup::Make();
  group->setElements(std::move(elements));
  return group;
}

// The standard panel: the given regions laid out in panel-local space at the standard
// offset, with the given styles attached.
static std::shared_ptr<Layer> MakePanel(
    const std::vector<RegionSpec>& regions,
    const std::vector<std::shared_ptr<LayerStyle>>& styles = {}) {
  std::vector<std::shared_ptr<VectorElement>> contents = {};
  for (const auto& region : regions) {
    contents.push_back(MakeRegion(region));
  }
  auto layer = VectorLayer::Make();
  layer->setContents(std::move(contents));
  layer->setMatrix(Matrix::MakeTrans(GlassPanelLeft, GlassPanelTop));
  if (!styles.empty()) {
    layer->setLayerStyles(styles);
  }
  return layer;
}

// ===== Shared coverage fixtures =====

// Every panel below is built on this mask-probe geometry, so the scans and the rendered regions
// describe the same layer-local coordinates.
static constexpr int MaskProbeSurfaceSize = 200;
static constexpr float MaskProbeRectLeft = 60.0f;
static constexpr float MaskProbeRectTop = 60.0f;
static constexpr float MaskProbeRectSize = 80.0f;

// How far the stroke band reaches beyond the shape boundary, per alignment.
static float MaskStrokeOutwardExtent(float strokeWidth, StrokeAlign align) {
  switch (align) {
    case StrokeAlign::Center:
      return strokeWidth * 0.5f;
    case StrokeAlign::Outside:
      return strokeWidth;
    case StrokeAlign::Inside:
      return 0.0f;
  }
  return 0.0f;
}

// Signed distance to a rounded rectangle with a uniform corner radius; negative inside. A sample
// point at distance <= -1 sits at least one pixel inside the boundary, clear of the anti-aliased
// band, so its coverage has to be exactly 1.
static float RoundedRectSignedDistance(const Rect& rect, float radius, float px, float py) {
  auto qx = fabsf(px - rect.centerX()) - (rect.width() * 0.5f - radius);
  auto qy = fabsf(py - rect.centerY()) - (rect.height() * 0.5f - radius);
  auto outsideX = std::max(qx, 0.0f);
  auto outsideY = std::max(qy, 0.0f);
  return sqrtf(outsideX * outsideX + outsideY * outsideY) + std::min(std::max(qx, qy), 0.0f) -
         radius;
}

// Renders the merged mask for one parameter combination: a single opaque, additively blended
// rounded-rectangle fill grown by `outward` on every side (size += 2 * outward, radius += outward),
// i.e. the geometry that merges a fill with its outside stroke band. Returns an empty vector on
// failure.
static std::vector<uint32_t> RenderMergedFillMask(Context* context, float radius, float outward) {
  std::vector<uint32_t> pixels = {};
  auto surface = Surface::Make(context, MaskProbeSurfaceSize, MaskProbeSurfaceSize);
  if (surface == nullptr) {
    return pixels;
  }
  auto layer = VectorLayer::Make();
  auto rect = Rectangle::Make();
  rect->setPosition(
      {MaskProbeRectLeft + MaskProbeRectSize * 0.5f, MaskProbeRectTop + MaskProbeRectSize * 0.5f});
  rect->setSize({MaskProbeRectSize + outward * 2.0f, MaskProbeRectSize + outward * 2.0f});
  rect->setRoundness(radius + outward);
  auto fill = FillStyle::Make(SolidColor::Make(Color::White()));
  fill->setAlpha(1.0f);
  fill->setBlendMode(BlendMode::PlusLighter);
  auto group = VectorGroup::Make();
  group->setElements({rect, fill});
  layer->setContents({group});
  auto displayList = std::make_unique<DisplayList>();
  // Direct mode draws straight onto the target without an intermediate cache, so the read-back
  // coverage is the raw coverage of the mask raster.
  displayList->setRenderMode(RenderMode::Direct);
  displayList->root()->addChild(layer);
  displayList->render(surface.get());
  auto info = ImageInfo::Make(MaskProbeSurfaceSize, MaskProbeSurfaceSize, ColorType::RGBA_8888,
                              AlphaType::Premultiplied);
  pixels.resize(static_cast<size_t>(MaskProbeSurfaceSize) * MaskProbeSurfaceSize);
  if (!surface->readPixels(info, pixels.data())) {
    pixels.clear();
  }
  return pixels;
}

static int MaskAlphaAt(const std::vector<uint32_t>& pixels, int x, int y) {
  if (x < 0 || y < 0 || x >= MaskProbeSurfaceSize || y >= MaskProbeSurfaceSize ||
      pixels.size() != static_cast<size_t>(MaskProbeSurfaceSize) * MaskProbeSurfaceSize) {
    return -1;
  }
  auto index = static_cast<size_t>(y) * MaskProbeSurfaceSize + static_cast<size_t>(x);
  return static_cast<int>((pixels[index] >> 24) & 0xFF);
}

// The stroke band of `align` in layer-local coordinates: its outward extent from the shape boundary
// plus its width is what decides where the band ends on both sides.
struct StrokeBand {
  float top;
  float bottom;
};

static StrokeBand BandOf(const Rect& rect, float strokeWidth, StrokeAlign align) {
  auto top = rect.top - MaskStrokeOutwardExtent(strokeWidth, align);
  return {top, top + strokeWidth};
}

// Coverage of the interior of a filled rounded rectangle: the pixels whose center sits at least one
// pixel inside the outer boundary, which is clear of the anti-aliased edge.
struct InteriorCoverage {
  int interior = 0;
  int belowFull = 0;
  int lowest = 255;
  int lowestX = -1;
  int lowestY = -1;
};

static InteriorCoverage ScanInteriorCoverage(const std::vector<uint32_t>& pixels,
                                             const Rect& outerRect, float outerRadius) {
  InteriorCoverage result = {};
  for (int y = 0; y < MaskProbeSurfaceSize; y++) {
    for (int x = 0; x < MaskProbeSurfaceSize; x++) {
      auto px = static_cast<float>(x) + 0.5f;
      auto py = static_cast<float>(y) + 0.5f;
      if (RoundedRectSignedDistance(outerRect, outerRadius, px, py) > -1.0f) {
        continue;
      }
      result.interior++;
      auto alpha = MaskAlphaAt(pixels, x, y);
      if (alpha == 255) {
        continue;
      }
      result.belowFull++;
      if (alpha < result.lowest) {
        result.lowest = alpha;
        result.lowestX = x;
        result.lowestY = y;
      }
    }
  }
  return result;
}

// A rectangle geometry for one group. The fill and the stroke need two separate instances: sharing
// one geometry element puts them in the same element list, and that shared-instance, single-group
// structure is what the stroke probe covers.
static std::shared_ptr<ShapePath> MakeRectShapePath(const Rect& rect, float radius) {
  auto shape = ShapePath::Make();
  Path path = {};
  path.addRRect(RRect::MakeRectXY(rect, radius, radius));
  shape->setPath(std::move(path));
  return shape;
}

// Which fill painter a panel carries next to its stroke: none, or a retained fully transparent fill.
enum class GeometryFillKind { None, Transparent };

// One rounded rectangle with an optional fill painter and one stroke painter of the given alignment.
// fillKind selects the painter recorded next to the stroke. styles are attached to the layer: with a
// Glass style the layer records its geometry, without one it renders as the glass-off reference.
static std::shared_ptr<Layer> MakeGeometryStrokePanel(
    const Rect& rect, float radius, float strokeWidth, StrokeAlign align, GeometryFillKind fillKind,
    const std::vector<std::shared_ptr<LayerStyle>>& styles = {
        GlassStyle::Make(0, 50, 100, 0, 0, 0, 0)}) {
  auto layer = VectorLayer::Make();
  auto shape = Rectangle::Make();
  shape->setPosition({rect.centerX(), rect.centerY()});
  shape->setSize({rect.width(), rect.height()});
  shape->setRoundness(radius);
  auto stroke = StrokeStyle::Make(SolidColor::Make(Color::White()));
  stroke->setAlpha(1.0f);
  stroke->setStrokeWidth(strokeWidth);
  stroke->setStrokeAlign(align);
  std::vector<std::shared_ptr<VectorElement>> elements = {shape};
  if (fillKind == GeometryFillKind::Transparent) {
    elements.push_back(MakeZeroAlphaFill());
  }
  elements.push_back(stroke);
  auto group = VectorGroup::Make();
  group->setElements(std::move(elements));
  layer->setContents({group});
  if (!styles.empty()) {
    layer->setLayerStyles(styles);
  }
  return layer;
}

// A fill painter and a differently typed stroke painter live in two sibling VectorGroups instead of
// one element list. withFillGroup=false drops the fill group and is the control that shows what a
// shape with no coverage inside its stroke band reads back as.
static std::shared_ptr<Layer> MakeTwoGroupFillStrokePanel(
    const std::shared_ptr<VectorElement>& fillGeometry, float fillAlpha,
    const std::shared_ptr<VectorElement>& strokeGeometry, float strokeWidth,
    const std::shared_ptr<GlassStyle>& glass, bool withFillGroup = true) {
  auto layer = VectorLayer::Make();
  std::vector<std::shared_ptr<VectorElement>> contents = {};
  if (withFillGroup) {
    auto fillGroup = VectorGroup::Make();
    auto fill = FillStyle::Make(SolidColor::Make(Color::White()));
    fill->setAlpha(fillAlpha);
    fillGroup->setElements({fillGeometry, fill});
    contents.push_back(fillGroup);
  }
  auto strokeGroup = VectorGroup::Make();
  // Translucent, so the Glass drawn under the band stays observable in a rendered comparison.
  auto stroke = StrokeStyle::Make(SolidColor::Make(Color::White()));
  stroke->setAlpha(0.4f);
  stroke->setStrokeWidth(strokeWidth);
  stroke->setStrokeAlign(StrokeAlign::Outside);
  stroke->setPlacement(LayerPlacement::Foreground);
  strokeGroup->setElements({strokeGeometry, stroke});
  contents.push_back(strokeGroup);
  layer->setContents(std::move(contents));
  if (glass != nullptr) {
    layer->setLayerStyles({glass});
  }
  return layer;
}

// Glass is built here with excludeChildEffects set, so the style sits in groups[1]. The geometry
// recording itself is layer-wide and shared by both buckets.
static std::shared_ptr<GlassStyle> MakeGeometryGlass(float refraction, float depth, float frost,
                                                     float dispersion, float splay) {
  auto glass = GlassStyle::Make(refraction, depth, frost, dispersion, splay, 0, 0);
  glass->setExcludeChildEffects(true);
  return glass;
}

// Which painter the child-A layer of a parent/child panel carries.
enum class GlassChildFillKind { None, Full, Donut };

// Parent + children tree shared by the parent/child cases: an optional child-A retained 0%-alpha fill
// (whole base rectangle or an annulus) plus the ring band on a second child, so the parent keeps no
// painter of its own. `style` may be null, which is the glass-off reference the rendered comparison
// subtracts.
static std::shared_ptr<Layer> MakeGlassChildPanel(GlassChildFillKind fillKind, const Rect& baseRect,
                                                  const Rect& holeRect, float ringWidth,
                                                  const std::shared_ptr<LayerStyle>& style) {
  auto parent = Layer::Make();
  if (fillKind != GlassChildFillKind::None) {
    std::shared_ptr<VectorElement> geometry = nullptr;
    if (fillKind == GlassChildFillKind::Full) {
      auto rect = Rectangle::Make();
      rect->setPosition({baseRect.centerX(), baseRect.centerY()});
      rect->setSize({baseRect.width(), baseRect.height()});
      geometry = rect;
    } else {
      Path donut = {};
      donut.setFillType(PathFillType::EvenOdd);
      donut.addRect(baseRect);
      donut.addRect(holeRect);
      auto shape = ShapePath::Make();
      shape->setPath(std::move(donut));
      geometry = shape;
    }
    auto fillLayer = VectorLayer::Make();
    // A retained 0%-alpha fill: style alpha 0 over a color alpha of 1, so drawDefault drops it from
    // `content` while drawContourInternal keeps it in `contour`.
    auto fill = FillStyle::Make(SolidColor::Make(Color::White()));
    fill->setAlpha(0.0f);
    fillLayer->setContents({MakeFilledRegion(geometry, fill)});
    parent->addChild(fillLayer);
  }
  // The ring band sits on a second child layer so the parent keeps no painter of its own; its color
  // alpha stays above 0, so both recordings hold it.
  auto ringLayer = VectorLayer::Make();
  auto ringRect = Rectangle::Make();
  ringRect->setPosition({baseRect.centerX(), baseRect.centerY()});
  ringRect->setSize({baseRect.width(), baseRect.height()});
  auto stroke = StrokeStyle::Make(SolidColor::Make(Color::White()));
  stroke->setAlpha(0.4f);
  stroke->setStrokeWidth(ringWidth);
  stroke->setStrokeAlign(StrokeAlign::Outside);
  auto ringGroup = VectorGroup::Make();
  ringGroup->setElements({ringRect, stroke});
  ringLayer->setContents({ringGroup});
  parent->addChild(ringLayer);
  if (style != nullptr) {
    parent->setLayerStyles({style});
  }
  return parent;
}

// A layer whose only content is a geometry no painter captures records no visible content, yet its
// geometry drives Glass: the coverage follows the geometry and nothing paints. This absorbs the
// geometry-only case and a two-region geometry with a hole.
TGFX_TEST_PRIVATE(LayerTest, GlassGeometryGivesCoverageWithoutAnyFill) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const auto panelBounds = GlassPanelBounds();
  const auto glass = GlassStyle::Make(0, 50, 100, 0, 0, 0, 0);
  auto interior = panelBounds;
  interior.inset(1.0f, 1.0f);
  const auto interiorArea =
      static_cast<int>(interior.width()) * static_cast<int>(interior.height());

  RegionSpec bareGeometry = {};
  bareGeometry.bounds = GlassPanelLocalBounds();

  auto plain = RenderPanelOverChecker(context, MakePanel({}));
  auto geometryOnly = RenderPanelOverChecker(context, MakePanel({bareGeometry}));
  auto glassPanel = RenderPanelOverChecker(context, MakePanel({bareGeometry}, {glass}));
  ASSERT_TRUE(plain != nullptr && geometryOnly != nullptr && glassPanel != nullptr);
  // The bare geometry paints nothing: adding it to a layer without a style changes no pixel.
  EXPECT_EQ(CountDifferingPixels(plain.get(), geometryOnly.get(), interior, 2), 0)
      << "geometry-only interior ROI: bare geometry must not paint anything";
  // With the Glass style the bare geometry drives real frost over the whole panel.
  const auto frosted = CountDifferingPixels(plain.get(), glassPanel.get(), interior, 16);
  EXPECT_GT(frosted, interiorArea / 5) << "geometry-only interior ROI: the geometry-driven Glass "
                                          "must frost a sizable part of the panel, frosted "
                                       << frosted << " of " << interiorArea;
  // The coverage ends at the geometry outline.
  EXPECT_EQ(CountDifferingPixels(
                plain.get(), glassPanel.get(),
                Rect::MakeXYWH(0.0f, 0.0f, GlassPanelLeft - 1.0f, GlassPanelSurfaceExtent), 2),
            0)
      << "left-of-panel ROI: the geometry-driven Glass must not reach outside its outline";

  DrawArgs drawArgs(context);
  TGFX_PRIVATE_ACCESS(
      // Failure attribution: bare geometry must not leak into the visible content, otherwise every
      // content-based style would see geometry the author never drew. This is a recording assertion
      // on purpose -- a leak would change rendered pixels only through some other style, which would
      // make the failure impossible to attribute.
      auto markedSource =
          MakePanel({bareGeometry}, {glass})->getLayerStyleSource(drawArgs, Matrix::I());
      ASSERT_TRUE(markedSource != nullptr); ASSERT_TRUE(markedSource->groups[0] != nullptr);
      EXPECT_TRUE(markedSource->groups[0]->content == nullptr)
      << "bare geometry must not be recorded in the shared content image";
      ASSERT_TRUE(markedSource->groups[0]->geometry != nullptr);
      EXPECT_TRUE(markedSource->groups[0]->geometry->image() != nullptr);
      EXPECT_TRUE(markedSource->groups[0]->geometry->shape().has_value())
      << "a single bare geometry is the whole geometry, so Glass gets an exact shape";)

  // Donut arm: a second region with a hole. The coverage follows the union of the two regions, so it
  // frosts both regions and leaves the hole untouched.
  const auto regionABounds = Rect::MakeLTRB(0.0f, 0.0f, 60.0f, 80.0f);
  const auto regionBBounds = Rect::MakeLTRB(80.0f, 0.0f, 140.0f, 80.0f);
  const auto holeBounds = Rect::MakeLTRB(100.0f, 25.0f, 120.0f, 55.0f);
  RegionSpec regionA = {};
  regionA.bounds = regionABounds;
  RegionSpec regionB = {};
  {
    Path donut = {};
    donut.setFillType(PathFillType::EvenOdd);
    donut.addRect(regionBBounds);
    donut.addRect(holeBounds);
    auto shape = ShapePath::Make();
    shape->setPath(std::move(donut));
    regionB.shape = shape;
  }
  auto donutPlain = RenderPanelOverChecker(context, MakePanel({regionA, regionB}));
  auto donutGlass = RenderPanelOverChecker(context, MakePanel({regionA, regionB}, {glass}));
  ASSERT_TRUE(donutPlain != nullptr && donutGlass != nullptr);
  auto worldA = regionABounds;
  worldA.offset(GlassPanelLeft, GlassPanelTop);
  worldA.inset(5.0f, 5.0f);
  const auto areaA = static_cast<int>(worldA.width() * worldA.height());
  EXPECT_GT(CountDifferingPixels(donutPlain.get(), donutGlass.get(), worldA, 16), areaA / 5)
      << "donut first-region ROI: the multi-region geometry must cover the first region";
  auto worldBandB = Rect::MakeLTRB(
      GlassPanelLeft + regionBBounds.left + 2.0f, GlassPanelTop + regionBBounds.top + 5.0f,
      GlassPanelLeft + holeBounds.left - 2.0f, GlassPanelTop + regionBBounds.bottom - 5.0f);
  const auto areaBandB = static_cast<int>(worldBandB.width() * worldBandB.height());
  EXPECT_GT(CountDifferingPixels(donutPlain.get(), donutGlass.get(), worldBandB, 16), areaBandB / 5)
      << "donut second-region band ROI: every region of the geometry must be covered";
  auto worldHole = holeBounds;
  worldHole.offset(GlassPanelLeft, GlassPanelTop);
  worldHole.inset(3.0f, 3.0f);
  EXPECT_EQ(CountDifferingPixels(donutPlain.get(), donutGlass.get(), worldHole, 2), 0)
      << "donut hole ROI: the hole in the geometry must stay untouched";
}

// The bare geometry belongs to the layer's geometry alone. Four rendered observables pin that on
// the recordings a content-based style consumes: a sibling spread shadow renders identically with and
// without the bare geometry (in either recording order, and for a region disjoint from the visible
// fill), the shadow follows the outside stroke band rather than the shape interior, and a real
// 0%-opacity fill does reach the shared contour the bare geometry does not.
TGFX_TEST_PRIVATE(LayerTest, GlassGeometryDoesNotLeakIntoSharedRecordings) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto shadow = DropShadowStyle::Make(0, 0, 0, 0, Color::Black());
  shadow->setSpread(10);
  RegionSpec panel = {};
  panel.bounds = GlassPanelLocalBounds();
  panel.roundness = 20.0f;
  panel.fill = RegionFillKind::Opaque;
  RegionSpec overlayGeometry = panel;
  overlayGeometry.fill = RegionFillKind::None;

  // The bare geometry overlays the panel's geometry, so the shadow's source shape and bounds are
  // the panel's whether or not the geometry is recorded; a leak would still move the shadow through
  // the grouping of the shared contour.
  auto makeShadowPanel = [&](bool withGeometry, bool geometryFirst) {
    std::vector<RegionSpec> regions = {};
    if (withGeometry && geometryFirst) {
      regions.push_back(overlayGeometry);
    }
    regions.push_back(panel);
    if (withGeometry && !geometryFirst) {
      regions.push_back(overlayGeometry);
    }
    return MakePanel(regions, {shadow});
  };
  // A probe just outside the rounded top-left corner: the spread reaches there, so the reading is
  // not vacuous.
  constexpr int cornerProbeX = 54;
  constexpr int cornerProbeY = 54;
  auto plainShadowPixel =
      RenderSpreadShadowPixel(context, makeShadowPanel(false, false), cornerProbeX, cornerProbeY);
  ASSERT_TRUE(plainShadowPixel.has_value());
  EXPECT_NE(*plainShadowPixel, 0u)
      << "rounded-corner probe: the spread shadow must reach just outside the panel corner";
  auto plainShadow = RenderPanelOverChecker(context, makeShadowPanel(false, false));
  ASSERT_TRUE(plainShadow != nullptr);
  const auto shadowRoi =
      Rect::MakeLTRB(0.0f, 0.0f, GlassPanelSurfaceExtent, GlassPanelSurfaceExtent);
  for (auto geometryFirst : {false, true}) {
    auto bareShadowPixel = RenderSpreadShadowPixel(context, makeShadowPanel(true, geometryFirst),
                                                   cornerProbeX, cornerProbeY);
    ASSERT_TRUE(bareShadowPixel.has_value());
    EXPECT_EQ(*bareShadowPixel, *plainShadowPixel)
        << "rounded-corner probe (geometryFirst=" << geometryFirst
        << "): the bare geometry must not move the shadow";
    auto bareShadow = RenderPanelOverChecker(context, makeShadowPanel(true, geometryFirst));
    ASSERT_TRUE(bareShadow != nullptr);
    EXPECT_EQ(CountDifferingPixels(plainShadow.get(), bareShadow.get(), shadowRoi, 2), 0)
        << "shadow ROI (geometryFirst=" << geometryFirst
        << "): the bare geometry must not change the shadow, in either recording order";
  }

  // Disjoint arm: the bare geometry sits on a region the visible fill does not cover, so a leak into
  // the content shape would take the exact outline away from the shadow. The shadow follows the
  // content shape: with the bare geometry left out it hugs the rounded fill, while a geometry that
  // leaked in would make the shape unavailable and let the shadow fall back to the content image's
  // bounding rect.
  RegionSpec disjointGeometry = {};
  disjointGeometry.bounds = Rect::MakeXYWH(130.0f, 10.0f, 40.0f, 40.0f);
  RegionSpec disjointUserFill = disjointGeometry;
  disjointUserFill.fill = RegionFillKind::Transparent;
  auto makeDisjointShadow = [&](const RegionSpec& second, bool withGeometry) {
    auto disjointShadow = DropShadowStyle::Make(0, 0, 0, 0, Color::Black());
    disjointShadow->setSpread(10);
    std::vector<RegionSpec> regions = {panel};
    if (withGeometry) {
      regions.push_back(second);
    }
    return MakePanel(regions, {disjointShadow});
  };
  auto fillOnlyShadow =
      RenderPanelOverChecker(context, makeDisjointShadow(disjointGeometry, false));
  auto disjointGeometryShadow =
      RenderPanelOverChecker(context, makeDisjointShadow(disjointGeometry, true));
  ASSERT_TRUE(fillOnlyShadow != nullptr && disjointGeometryShadow != nullptr);
  EXPECT_EQ(CountDifferingPixels(fillOnlyShadow.get(), disjointGeometryShadow.get(), shadowRoi, 2),
            0)
      << "disjoint-geometry shadow ROI: bare geometry must not enlarge the content shape, so the "
         "shadow footprint must stay the visible fill's";
  // Non-vacuity: a real 0%-opacity fill on the same disjoint region is captured by a painter, so it
  // does join the content shape and takes the exact outline away. That pixel change is exactly what
  // a leaked geometry would produce, so the equality above is a real skip.
  auto disjointUserFillShadow =
      RenderPanelOverChecker(context, makeDisjointShadow(disjointUserFill, true));
  ASSERT_TRUE(disjointUserFillShadow != nullptr);
  EXPECT_GT(CountDifferingPixels(fillOnlyShadow.get(), disjointUserFillShadow.get(), shadowRoi, 2),
            0)
      << "disjoint-geometry shadow ROI (fill control): a second region captured by a painter must "
         "change the shadow footprint, otherwise the equality above is vacuous";

  // A content-based style consumes the shared contour, whose outside stroke band keeps the requested
  // width: the band is a ring and not the shape's envelope, so a spread shadow must stay out of the
  // shape interior.
  constexpr float bandStrokeWidth = 8.0f;
  RegionSpec bandPanel = {};
  bandPanel.bounds = GlassPanelLocalBounds();
  bandPanel.strokeWidth = bandStrokeWidth;
  bandPanel.strokeAlign = StrokeAlign::Outside;
  auto bandShadow = DropShadowStyle::Make(0, 0, 0, 0, Color::Black());
  bandShadow->setSpread(10);
  auto bandShadowLayer = MakePanel({bandPanel}, {bandShadow});
  auto bandPixel = RenderSpreadShadowPixel(context, bandShadowLayer, 100, 45);
  ASSERT_TRUE(bandPixel.has_value());
  EXPECT_NE(*bandPixel, 0u) << "stroke-band probe: the spread shadow must render over the band the "
                               "shared recording holds";
  auto bandInteriorPixel = RenderSpreadShadowPixel(context, bandShadowLayer, 100, 90);
  ASSERT_TRUE(bandInteriorPixel.has_value());
  EXPECT_EQ(*bandInteriorPixel, 0u)
      << "deep-interior probe: the shadow must follow the band and not the shape interior";

  const auto secondBounds = Rect::MakeXYWH(105.0f, 10.0f, 40.0f, 40.0f);
  auto makeMaskedPanel = [&](RegionFillKind secondKind) {
    RegionSpec second = {};
    second.bounds = secondBounds;
    second.fill = secondKind;
    auto maskedShadow = DropShadowStyle::Make(0, 0, 0, 0, Color::Black());
    maskedShadow->setSpread(10);
    maskedShadow->setShowBehindLayer(false);
    return MakePanel({panel, second}, {maskedShadow});
  };
  // A shadow drawn behind the layer is masked by the shared contour, so it shows exactly where that
  // contour is ABSENT. Extending the contour with the second region therefore removes the shadow over
  // the region: the painter-captured 0%-opacity fill must stay in the contour and remove it, while
  // the bare geometry must leave the shadow alone. The sliver is the part of the second region the
  // spread shape of the panel also covers.
  const auto sliverRoi = Rect::MakeLTRB(
      secondBounds.left + GlassPanelLeft + 1.0f, secondBounds.top + GlassPanelTop + 5.0f,
      secondBounds.left + GlassPanelLeft + 6.0f, secondBounds.top + GlassPanelTop + 35.0f);
  constexpr int sliverProbeX = 157;
  constexpr int sliverProbeY = 80;
  auto bareSliver = RenderSpreadShadowPixel(context, makeMaskedPanel(RegionFillKind::None),
                                            sliverProbeX, sliverProbeY);
  ASSERT_TRUE(bareSliver.has_value());
  EXPECT_NE(*bareSliver, 0u)
      << "contour-sliver probe (bare geometry): bare geometry must not extend the shared contour, "
         "so the shadow behind the layer still renders there";
  auto transparentSliver = RenderSpreadShadowPixel(
      context, makeMaskedPanel(RegionFillKind::Transparent), sliverProbeX, sliverProbeY);
  ASSERT_TRUE(transparentSliver.has_value());
  EXPECT_EQ(*transparentSliver, 0u)
      << "contour-sliver probe (0% fill): a 0%-alpha fill must stay in the shared contour and mask "
         "the shadow over its region away";
  auto bareMasked = RenderPanelOverChecker(context, makeMaskedPanel(RegionFillKind::None));
  auto transparentMasked =
      RenderPanelOverChecker(context, makeMaskedPanel(RegionFillKind::Transparent));
  ASSERT_TRUE(bareMasked != nullptr && transparentMasked != nullptr);
  EXPECT_GT(CountDifferingPixels(bareMasked.get(), transparentMasked.get(), sliverRoi, 2), 0)
      << "contour-sliver ROI: the 0%-opacity fill must extend the shared contour while the bare "
         "geometry does not";
}

// A visible outside stroke is the shape the shared content and contour contain, so a Glass driven by
// them would cover the stroke ring alone. The geometry is the union of the 0%-alpha fill's
// geometry and the stroke, which is what makes the Glass cover the whole shape.
TGFX_TEST_PRIVATE(LayerTest, GlassGeometryKeepsInteriorCoverageWithAStroke) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  constexpr float strokeWidth = 8.0f;
  const auto glass = GlassStyle::Make(0, 50, 100, 0, 0, 0, 0);
  RegionSpec withStroke = {};
  withStroke.bounds = GlassPanelLocalBounds();
  // A retained 0%-alpha fill: the stroke already captures the geometry, so here only a painter
  // keeps the interior in the geometry.
  withStroke.fill = RegionFillKind::Transparent;
  withStroke.strokeWidth = strokeWidth;
  withStroke.strokeAlign = StrokeAlign::Outside;
  RegionSpec strokeOnly = withStroke;
  strokeOnly.fill = RegionFillKind::None;

  auto deepInterior = GlassPanelBounds();
  deepInterior.inset(strokeWidth * 2.0f, strokeWidth * 2.0f);
  const auto deepArea =
      static_cast<int>(deepInterior.width()) * static_cast<int>(deepInterior.height());
  const auto bandRoi =
      Rect::MakeXYWH(GlassPanelLeft, GlassPanelTop - strokeWidth, GlassPanelWidth, strokeWidth);

  auto control = RenderPanelOverChecker(context, MakePanel({withStroke}));
  auto glassPanel = RenderPanelOverChecker(context, MakePanel({withStroke}, {glass}));
  ASSERT_TRUE(control != nullptr && glassPanel != nullptr);
  // The interior is inset well past the stroke, so it is covered by the 0%-alpha fill alone: a
  // Glass limited to the stroke ring would leave it untouched.
  const auto frostedDeep = CountDifferingPixels(control.get(), glassPanel.get(), deepInterior, 16);
  EXPECT_GT(frostedDeep, deepArea / 5)
      << "deep-interior ROI (0% fill + outside stroke): the Glass must cover the shape inside the "
         "stroke, not only the stroke ring, frosted "
      << frostedDeep << " of " << deepArea;
  // The coverage ends at the geometry: a ring-only coverage would spill the effect outside the
  // stroke, where the filled shape has nothing.
  EXPECT_EQ(CountDifferingPixels(control.get(), glassPanel.get(),
                                 Rect::MakeXYWH(GlassPanelLeft - 19.0f, GlassPanelTop - 19.0f,
                                                10.0f, GlassPanelHeight + 38.0f),
                                 2),
            0)
      << "left-of-panel ROI: the Glass must not cover anything outside the shape and its stroke";
  // The geometry is the geometric union of the fill geometry and the outside stroke, so the
  // shape is a fill-plus-stroke. A wrong classification as a plain fill would make Glass take the
  // analytic SDF of the base path alone, leaving the band outside that path uncovered: bandRoi sits
  // entirely inside the band, whose outer edge is strokeWidth above the panel top.
  EXPECT_GT(CountDifferingPixels(control.get(), glassPanel.get(), bandRoi, 2), 0)
      << "stroke-band ROI (0% fill + outside stroke): a wrong geometry shape classification "
         "would "
         "leave the band outside the base path uncovered";

  // Control: with the fill removed the geometry is the stroke band alone, so the interior has to
  // stay untouched while the band still lights up.
  auto bandControl = RenderPanelOverChecker(context, MakePanel({strokeOnly}));
  auto bandGlass = RenderPanelOverChecker(context, MakePanel({strokeOnly}, {glass}));
  ASSERT_TRUE(bandControl != nullptr && bandGlass != nullptr);
  EXPECT_EQ(CountDifferingPixels(bandControl.get(), bandGlass.get(), deepInterior, 16), 0)
      << "deep-interior ROI (fill removed): the geometry is the band alone and must not reach "
         "into the shape";
  EXPECT_GT(CountDifferingPixels(bandControl.get(), bandGlass.get(), bandRoi, 16), 0)
      << "stroke-band ROI (fill removed): the band must still be covered, otherwise the interior "
         "reading is vacuous";
}

// One region whose geometry is `path` and which carries `fill`, so path-shaped cases still route
// through MakeRegion instead of assembling their own VectorGroup.
static RegionSpec MakePathRegion(const Path& path, RegionFillKind fill = RegionFillKind::None) {
  auto shapePath = ShapePath::Make();
  shapePath->setPath(path);
  RegionSpec spec = {};
  spec.shape = shapePath;
  spec.fill = fill;
  return spec;
}

// Two triangles that meet on the diagonal y = x, plus that same square as a single path. Shared by
// the arms that compare "two geometries describing one square" against "one square".
struct SplitSquarePaths {
  Path upperRight = {};
  Path lowerLeft = {};
  Path square = {};
};

static SplitSquarePaths MakeSplitSquarePaths(float size) {
  SplitSquarePaths paths = {};
  paths.upperRight.moveTo(0.0f, 0.0f);
  paths.upperRight.lineTo(size, 0.0f);
  paths.upperRight.lineTo(size, size);
  paths.upperRight.close();
  paths.lowerLeft.moveTo(0.0f, 0.0f);
  paths.lowerLeft.lineTo(0.0f, size);
  paths.lowerLeft.lineTo(size, size);
  paths.lowerLeft.close();
  paths.square.addRect(Rect::MakeWH(size, size));
  return paths;
}

// The geometry rasterizes an opaque, hard-edged coverage: the opaque brush disables
// anti-aliasing, so a painter's boundary pixel is fully covered or not covered at all, and
// abutting painters tile the plane exactly. A rendered comparison can therefore see the coverage's
// REACH -- every interior pixel has to change and nothing outside the outline may -- and the
// shared-boundary arm pins that two geometries describing one square render like the single square,
// through both recording channels (see the shared-boundary case below for why each one is run).
TGFX_TEST_PRIVATE(LayerTest, GeometryCoversItsPainters) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const Rect rect =
      Rect::MakeXYWH(MaskProbeRectLeft, MaskProbeRectTop, MaskProbeRectSize, MaskProbeRectSize);
  struct Probe {
    float radius;
    float strokeWidth;
    StrokeAlign align;
  };
  // Outside alignment is where a transparent fill and the stroke band are two separate areas meeting
  // on the shape boundary; Center and Inside keep the stroke on or inside it and act as controls. The
  // corner radius is covered on both sides of the point where the stroker's inner offset collapses.
  const Probe probes[] = {
      {24.0f, 8.0f, StrokeAlign::Outside}, {12.0f, 4.0f, StrokeAlign::Outside},
      {6.0f, 1.0f, StrokeAlign::Outside},  {8.0f, 8.0f, StrokeAlign::Outside},
      {2.0f, 8.0f, StrokeAlign::Outside},  {24.0f, 8.0f, StrokeAlign::Center},
      {24.0f, 8.0f, StrokeAlign::Inside},  {6.0f, 1.0f, StrokeAlign::Center},
  };
  // A retained 0%-alpha fill keeps the geometry of the shape inside the stroke band in the
  // geometry; without one the stroke painter is the only outline and the interior is uncovered.
  const auto fillKind = GeometryFillKind::Transparent;

  // The rendered coverage reaches the whole interior and stops at the outer boundary.
  for (const auto& probe : probes) {
    auto outward = MaskStrokeOutwardExtent(probe.strokeWidth, probe.align);
    auto outsideRoi = Rect::MakeLTRB(0.0f, 0.0f, rect.left - outward - 2.0f, MaskProbeSurfaceSize);
    auto interiorRoi = rect;
    interiorRoi.inset(probe.radius * 0.5f + outward + 2.0f, probe.radius * 0.5f + outward + 2.0f);
    ASSERT_FALSE(interiorRoi.isEmpty());
    const auto interiorArea =
        static_cast<int>(interiorRoi.width()) * static_cast<int>(interiorRoi.height());
    const auto glass = GlassStyle::Make(0, 50, 100, 0, 0, 0, 0);
    auto plain = RenderPanelOverChecker(
        context,
        MakeGeometryStrokePanel(rect, probe.radius, probe.strokeWidth, probe.align, fillKind, {}));
    auto styled = RenderPanelOverChecker(
        context, MakeGeometryStrokePanel(rect, probe.radius, probe.strokeWidth, probe.align,
                                         fillKind, {glass}));
    ASSERT_TRUE(plain != nullptr && styled != nullptr);
    const auto interiorDiff = CountDifferingPixels(plain.get(), styled.get(), interiorRoi, 16);
    EXPECT_EQ(interiorDiff, interiorArea)
        << "interior ROI, r=" << probe.radius << " w=" << probe.strokeWidth
        << " align=" << static_cast<int>(probe.align)
        << ": every interior pixel must change, so the geometry covers the whole interior: "
        << interiorDiff << " of " << interiorArea << " pixels changed";
    EXPECT_EQ(CountDifferingPixels(plain.get(), styled.get(), outsideRoi, 2), 0)
        << "outside ROI, r=" << probe.radius << " w=" << probe.strokeWidth
        << " align=" << static_cast<int>(probe.align)
        << ": the coverage must stop at the outer boundary";
  }

  // Two geometries that share a boundary. Two triangles meet on the diagonal y = x, so together they
  // describe the same square as the single geometry below. Because the geometry recording is
  // hard-edged, each boundary pixel belongs to one triangle, and the two-geometry outline must
  // render like the single square. Both recording channels run the same comparison: bare geometry,
  // and a retained 0%-alpha painter that captures the geometry -- the channel whose two abutting
  // painters would leave a 0.75 seam on the diagonal if the outline were per-painter.
  constexpr float panelSize = 64.0f;
  const auto paths = MakeSplitSquarePaths(panelSize);
  const auto sharedBoundaryGlass = GlassStyle::Make(0, 50, 100, 0, 0, 0, 0);
  const auto sharedBoundaryRoi =
      Rect::MakeLTRB(GlassPanelLeft + 4.0f, GlassPanelTop + 4.0f, GlassPanelLeft + panelSize - 4.0f,
                     GlassPanelTop + panelSize - 4.0f);
  const auto sharedBoundaryArea =
      static_cast<int>(sharedBoundaryRoi.width()) * static_cast<int>(sharedBoundaryRoi.height());
  const auto twoPartQuadrantRoi =
      Rect::MakeLTRB(GlassPanelLeft + 8.0f, GlassPanelTop + 8.0f, GlassPanelLeft + panelSize * 0.5f,
                     GlassPanelTop + panelSize * 0.5f);
  auto emptyPanel = RenderPanelOverChecker(context, MakePanel({}));
  ASSERT_TRUE(emptyPanel != nullptr);
  for (auto fillKind : {RegionFillKind::None, RegionFillKind::Transparent}) {
    auto twoGeometries =
        RenderPanelOverChecker(context, MakePanel({MakePathRegion(paths.upperRight, fillKind),
                                                   MakePathRegion(paths.lowerLeft, fillKind)},
                                                  {sharedBoundaryGlass}));
    auto oneGeometry = RenderPanelOverChecker(
        context, MakePanel({MakePathRegion(paths.square, fillKind)}, {sharedBoundaryGlass}));
    ASSERT_TRUE(twoGeometries != nullptr && oneGeometry != nullptr);
    EXPECT_EQ(CountDifferingPixels(twoGeometries.get(), oneGeometry.get(), sharedBoundaryRoi, 4), 0)
        << "shared-boundary ROI, fill=" << static_cast<int>(fillKind)
        << ": two geometries describing the same square must render like the single square";
    EXPECT_GT(CountDifferingPixels(emptyPanel.get(), twoGeometries.get(), twoPartQuadrantRoi, 16),
              sharedBoundaryArea / 20)
        << "two-part quadrant ROI, fill=" << static_cast<int>(fillKind)
        << ": the two-part outline must cover its interior, otherwise the equality above is "
           "vacuous";
  }
}

// A style only covers what its geometry holds, so the two-group fill/stroke assembly has to
// keep the fill geometry. The arms are the refractive parameter set (100/100/14/100) on
// a rounded rectangle, the frost-only path at both frost values, and an ellipse, plus a two-region
// panel whose second region carries a 0%-alpha fill. Every arm pairs its assertion with a band-only
// control, so a covered shape cannot pass on a missing fill geometry.
TGFX_TEST_PRIVATE(LayerTest, TwoGroupAssemblyKeepsInteriorCoverage) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const Rect rect =
      Rect::MakeXYWH(MaskProbeRectLeft, MaskProbeRectTop, MaskProbeRectSize, MaskProbeRectSize);
  const float radius = 24.0f;
  const float strokeWidth = 8.0f;
  auto outward = MaskStrokeOutwardExtent(strokeWidth, StrokeAlign::Outside);
  auto band = BandOf(rect, strokeWidth, StrokeAlign::Outside);
  auto bandRoi = Rect::MakeLTRB(rect.left, band.top, rect.right, band.bottom);
  auto deepInterior = rect;
  deepInterior.inset(strokeWidth * 2.0f, strokeWidth * 2.0f);
  const auto deepArea =
      static_cast<int>(deepInterior.width()) * static_cast<int>(deepInterior.height());
  auto outsideRoi = Rect::MakeLTRB(0.0f, 0.0f, rect.left - outward - 2.0f, MaskProbeSurfaceSize);

  struct Arm {
    const char* label;
    bool ellipse;
    float refraction;
    float depth;
    float frost;
    float dispersion;
    float splay;
  };
  const Arm arms[] = {
      {"two-group rect refraction 100/100/14/100", false, 100, 100, 14, 100, 0},
      {"two-group rect frost 100", false, 0, 0, 100, 0, 0},
      {"two-group rect frost 4", false, 0, 0, 4, 0, 0},
      {"two-group ellipse frost-only 0/100/32/0/16", true, 0, 100, 32, 0, 16},
  };

  for (const auto& arm : arms) {
    auto glass = MakeGeometryGlass(arm.refraction, arm.depth, arm.frost, arm.dispersion, arm.splay);
    auto makePanel = [&](bool withFillGroup, const std::shared_ptr<GlassStyle>& style) {
      if (arm.ellipse) {
        auto makeEllipse = [&]() {
          auto shape = Ellipse::Make();
          shape->setPosition({rect.centerX(), rect.centerY()});
          shape->setSize({rect.width(), rect.height()});
          return shape;
        };
        return MakeTwoGroupFillStrokePanel(makeEllipse(), 0.0f, makeEllipse(), strokeWidth, style,
                                           withFillGroup);
      }
      return MakeTwoGroupFillStrokePanel(MakeRectShapePath(rect, radius), 0.0f,
                                         MakeRectShapePath(rect, radius), strokeWidth, style,
                                         withFillGroup);
    };
    auto armInterior =
        arm.ellipse ? Rect::MakeXYWH(rect.centerX() - 20.0f, rect.centerY() - 20.0f, 40.0f, 40.0f)
                    : deepInterior;
    const auto armArea =
        static_cast<int>(armInterior.width()) * static_cast<int>(armInterior.height());

    auto styled = RenderPanelOverChecker(context, makePanel(true, glass));
    auto plain = RenderPanelOverChecker(context, makePanel(true, nullptr));
    ASSERT_TRUE(styled != nullptr && plain != nullptr) << arm.label;
    const auto interiorDiff = CountDifferingPixels(plain.get(), styled.get(), armInterior, 16);
    EXPECT_GT(interiorDiff, armArea / 5)
        << "interior ROI (" << arm.label
        << "): the Glass must cover the whole two-group shape and not only its stroke band: "
        << interiorDiff << " of " << armArea << " interior pixels changed";
    EXPECT_EQ(CountDifferingPixels(plain.get(), styled.get(), outsideRoi, 2), 0)
        << "outside ROI (" << arm.label << "): the coverage must stop at the outer boundary";

    // Band-only control: the same Glass over the same stroke without the fill group stays inside the
    // band, so the interior comparison above cannot pass on a band-only coverage.
    auto bandStyled = RenderPanelOverChecker(context, makePanel(false, glass));
    auto bandPlain = RenderPanelOverChecker(context, makePanel(false, nullptr));
    ASSERT_TRUE(bandStyled != nullptr && bandPlain != nullptr) << arm.label;
    EXPECT_GT(CountDifferingPixels(bandPlain.get(), bandStyled.get(), bandRoi, 2), 0)
        << "band ROI (band-only control, " << arm.label
        << "): the control Glass must still frost the stroke band it does cover";
    EXPECT_EQ(CountDifferingPixels(bandPlain.get(), bandStyled.get(), armInterior, 16), 0)
        << "interior ROI (band-only control, " << arm.label
        << "): without the fill group the Glass must not reach into the shape";
  }

  // Two-region panel: the 0% region has to keep its interior coverage when a second, filled region is
  // recorded in the same geometry, and dropping the 0% group has to leave the region uncovered.
  const auto secondRect = Rect::MakeXYWH(10.0f, 10.0f, 40.0f, 40.0f);
  auto multiRegionGlass = MakeGeometryGlass(100, 100, 14, 100, 0);
  auto makeMultiRegion = [&](bool withZeroAlphaFill, bool withGlass) {
    std::vector<std::shared_ptr<VectorElement>> contents = {};
    if (withZeroAlphaFill) {
      auto fillGroup = VectorGroup::Make();
      auto fill = FillStyle::Make(SolidColor::Make(Color::White()));
      fill->setAlpha(0.0f);
      fillGroup->setElements({MakeRectShapePath(rect, radius), fill});
      contents.push_back(fillGroup);
    }
    auto strokeGroup = VectorGroup::Make();
    // Translucent, so the Glass drawn under the band stays observable in a rendered comparison.
    auto stroke = StrokeStyle::Make(SolidColor::Make(Color::White()));
    stroke->setAlpha(0.4f);
    stroke->setStrokeWidth(strokeWidth);
    stroke->setStrokeAlign(StrokeAlign::Outside);
    stroke->setPlacement(LayerPlacement::Foreground);
    strokeGroup->setElements({MakeRectShapePath(rect, radius), stroke});
    contents.push_back(strokeGroup);
    auto secondGroup = VectorGroup::Make();
    auto secondFill = FillStyle::Make(SolidColor::Make(Color::White()));
    secondFill->setAlpha(0.4f);
    secondGroup->setElements({MakeRectShapePath(secondRect, 0.0f), secondFill});
    contents.push_back(secondGroup);
    auto layer = VectorLayer::Make();
    layer->setContents(std::move(contents));
    if (withGlass) {
      layer->setLayerStyles({multiRegionGlass});
    }
    return layer;
  };
  auto regionStyled = RenderPanelOverChecker(context, makeMultiRegion(true, true));
  auto regionPlain = RenderPanelOverChecker(context, makeMultiRegion(true, false));
  auto secondStyled = RenderPanelOverChecker(context, makeMultiRegion(false, true));
  auto secondPlain = RenderPanelOverChecker(context, makeMultiRegion(false, false));
  ASSERT_TRUE(regionStyled != nullptr && regionPlain != nullptr && secondStyled != nullptr &&
              secondPlain != nullptr);
  const auto regionDiff =
      CountDifferingPixels(regionPlain.get(), regionStyled.get(), deepInterior, 16);
  EXPECT_GT(regionDiff, deepArea / 5)
      << "interior ROI (multi-region): the Glass must cover the whole 0% region and not only its "
         "stroke band: "
      << regionDiff << " of " << deepArea << " interior pixels changed";
  EXPECT_GT(CountDifferingPixels(regionPlain.get(), regionStyled.get(), bandRoi, 2), 0)
      << "band ROI (multi-region): the control Glass must still frost the stroke band it does "
         "cover";
  EXPECT_GT(CountDifferingPixels(secondPlain.get(), secondStyled.get(), bandRoi, 2), 0)
      << "band ROI (multi-region, 0% group dropped): the second region must keep the outline alive";
  EXPECT_EQ(CountDifferingPixels(secondPlain.get(), secondStyled.get(), deepInterior, 16), 0)
      << "interior ROI (multi-region, 0% group dropped): without the 0% fill group the Glass must "
         "not reach into the region";
}

// The excludeChildEffects buckets are recorded separately, so a bucket that records nothing only
// drops itself: a Glass in the other bucket still paints. Without any Glass the original fail-closed
// contract stays, so a bucket the previous code left undrawn is never revived.
TGFX_TEST_PRIVATE(LayerTest, UnusableBucketOnlyDropsItself) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // --- With Glass: the spread DropShadow is in groups[1] and records nothing; geometry is
  // recorded for the whole layer, so the Glass in groups[0] still drives coverage.
  RegionSpec bareGeometry = {};
  bareGeometry.bounds = GlassPanelLocalBounds();
  auto shadow = DropShadowStyle::Make(0, 8, 0, 8, Color::Black());
  shadow->setSpread(10);
  shadow->setExcludeChildEffects(true);
  auto glass = GlassStyle::Make(0, 50, 100, 0, 0, 0, 0);
  auto shadowOnly = RenderPanelOverChecker(context, MakePanel({bareGeometry}, {shadow}));
  auto withGlass = RenderPanelOverChecker(context, MakePanel({bareGeometry}, {shadow, glass}));
  ASSERT_TRUE(shadowOnly != nullptr && withGlass != nullptr);
  auto panelInterior = GlassPanelBounds();
  panelInterior.inset(1.0f, 1.0f);
  const auto frosted = CountDifferingPixels(shadowOnly.get(), withGlass.get(), panelInterior, 16);
  EXPECT_GT(frosted, GlassPanelInteriorPixels / 20)
      << "panel interior ROI: the Glass in the other bucket must still paint even though its "
         "sibling bucket recorded nothing, frosted "
      << frosted << " of " << GlassPanelInteriorPixels;

  // --- Without Glass: a child that only holds bare geometry leaves both recordings of the parent
  // empty, so the whole source must stay unusable and the frame has to render like a pristine tree.
  auto makeTree = [&](bool emptyChild, bool withStyles) {
    auto layer = VectorLayer::Make();
    layer->setMatrix(Matrix::MakeTrans(GlassPanelLeft, GlassPanelTop));
    auto child = VectorLayer::Make();
    auto rect = Rectangle::Make();
    rect->setPosition({GlassPanelWidth * 0.5f, GlassPanelHeight * 0.5f});
    rect->setSize({GlassPanelWidth, GlassPanelHeight});
    std::vector<std::shared_ptr<VectorElement>> childContents = {rect};
    if (!emptyChild) {
      childContents.push_back(FillStyle::Make(SolidColor::Make(Color::White())));
    }
    child->setContents(std::move(childContents));
    layer->addChild(child);
    if (withStyles) {
      // One style per excludeChildEffects bucket: bucket-0 needs only the content image, bucket-1
      // also the contour (spread makes it Contour-sourced). A child holding only bare geometry
      // paints nothing, so both recordings are empty and neither bucket is usable.
      auto contentStyle = DropShadowStyle::Make(0, 0, 0, 0, Color::Black());
      auto contourStyle = DropShadowStyle::Make(0, 0, 0, 0, Color::Black());
      contourStyle->setSpread(10);
      contourStyle->setExcludeChildEffects(true);
      layer->setLayerStyles({contentStyle, contourStyle});
    }
    return layer;
  };
  const auto full = Rect::MakeLTRB(0.0f, 0.0f, GlassPanelSurfaceExtent, GlassPanelSurfaceExtent);
  auto emptyChildStyled = RenderPanelOverChecker(context, makeTree(true, true));
  auto emptyChildPristine = RenderPanelOverChecker(context, makeTree(true, false));
  ASSERT_TRUE(emptyChildStyled != nullptr && emptyChildPristine != nullptr);
  EXPECT_EQ(CountDifferingPixels(emptyChildStyled.get(), emptyChildPristine.get(), full, 2), 0)
      << "empty-child frame ROI: one unusable bucket must fail the whole source closed, so the "
         "styled tree renders like the pristine one";
  auto normalStyled = RenderPanelOverChecker(context, makeTree(false, true));
  auto normalPristine = RenderPanelOverChecker(context, makeTree(false, false));
  ASSERT_TRUE(normalStyled != nullptr && normalPristine != nullptr);
  EXPECT_GT(CountDifferingPixels(normalStyled.get(), normalPristine.get(), full, 16), 0)
      << "normal-child frame ROI: with a recorded child both buckets are usable, so the styled "
         "tree must differ from the pristine one";
}

// A parent whose geometry lives in child layers records no geometry, so Glass falls back to the
// lazily recorded shared subtree contour. That fallback keeps a child's retained 0%-alpha fill, which
// survives only in the contour.
TGFX_TEST_PRIVATE(LayerTest, GlassWithoutGeometryResolvesToSharedContour) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const Rect baseRect =
      Rect::MakeXYWH(MaskProbeRectLeft, MaskProbeRectTop, MaskProbeRectSize, MaskProbeRectSize);
  constexpr float ringWidth = 10.0f;
  const auto baseInteriorRoi = Rect::MakeLTRB(baseRect.left + 2.0f, baseRect.top + 2.0f,
                                              baseRect.right - 2.0f, baseRect.bottom - 2.0f);
  const auto interiorArea =
      static_cast<int>(baseInteriorRoi.width()) * static_cast<int>(baseInteriorRoi.height());
  const Rect holeRect = Rect::MakeXYWH(baseRect.left + 25.0f, baseRect.top + 25.0f,
                                       baseRect.width() - 50.0f, baseRect.height() - 50.0f);
  const auto holeRoi = Rect::MakeLTRB(holeRect.left + 2.0f, holeRect.top + 2.0f,
                                      holeRect.right - 2.0f, holeRect.bottom - 2.0f);
  const auto annulusTopRoi = Rect::MakeLTRB(baseRect.left + 2.0f, baseRect.top + 2.0f,
                                            baseRect.right - 2.0f, holeRect.top - 2.0f);
  const auto glass = MakeGeometryGlass(0, 50, 100, 0, 0);

  auto renderDiff = [&](GlassChildFillKind fillKind, const Rect& roi, int tolerance) {
    auto styled = RenderPanelOverChecker(
        context, MakeGlassChildPanel(fillKind, baseRect, holeRect, ringWidth, glass));
    auto plain = RenderPanelOverChecker(
        context, MakeGlassChildPanel(fillKind, baseRect, holeRect, ringWidth, nullptr));
    if (styled == nullptr || plain == nullptr) {
      return -1;
    }
    return CountDifferingPixels(plain.get(), styled.get(), roi, tolerance);
  };

  // Core end-to-end proof: with the retained 0% fill the base interior has to light up.
  const auto interiorWithFill = renderDiff(GlassChildFillKind::Full, baseInteriorRoi, 16);
  ASSERT_GE(interiorWithFill, 0);
  EXPECT_GT(interiorWithFill, interiorArea / 5)
      << "base interior ROI (0% child fill): the Glass must cover the previously invisible 0%-fill "
         "area and not just the ring: "
      << interiorWithFill << " of " << interiorArea << " interior pixels changed";
  // Control: without the fill painter the recordings hold the ring alone, so the interior stays
  // untouched and the comparison above cannot pass on ring-only coverage.
  const auto interiorNoFill = renderDiff(GlassChildFillKind::None, baseInteriorRoi, 16);
  ASSERT_GE(interiorNoFill, 0);
  EXPECT_EQ(interiorNoFill, 0)
      << "base interior ROI (no-fill control): without the fill painter the Glass must stay "
         "ring-only and leave the interior untouched";
  // An annulus base must keep its hole out of the coverage; the band reading proves the hole reading
  // is not vacuous.
  const auto annulusWithFill = renderDiff(GlassChildFillKind::Donut, annulusTopRoi, 16);
  const auto holeWithFill = renderDiff(GlassChildFillKind::Donut, holeRoi, 16);
  ASSERT_GE(annulusWithFill, 0);
  ASSERT_GE(holeWithFill, 0);
  EXPECT_GT(annulusWithFill, 0) << "annulus band ROI (0% donut fill): the band must be covered, "
                                   "otherwise the hole reading is "
                                   "vacuous";
  EXPECT_EQ(holeWithFill, 0)
      << "annulus hole ROI (0% donut fill): the hole must stay uncovered, so the coverage follows "
         "the fill geometry and not its bounding box";

  // Failure attribution: the fallback picked the lazily recorded shared contour rather than the
  // shared content. This is a recording assertion on purpose -- for a plainly visible child both
  // recordings carry the same geometry, so the rendered result cannot tell which one Glass was
  // handed. A non-Geometry style shares the bucket so that the shared content is recorded too, which
  // is what makes the choice below observable.
  DrawArgs drawArgs(context);
  auto panel = MakeGlassChildPanel(GlassChildFillKind::Full, baseRect, holeRect, ringWidth, glass);
  // A non-Geometry style sharing the bucket keeps the shared content recorded, so the resolution
  // below really has two recordings to choose from.
  auto shadow = DropShadowStyle::Make(0, 0, 0, 0, Color::Black());
  shadow->setExcludeChildEffects(true);
  panel->setLayerStyles({glass, shadow});
  TGFX_PRIVATE_ACCESS(
      auto source = panel->getLayerStyleSource(drawArgs, Matrix::I());
      ASSERT_TRUE(source != nullptr); ASSERT_TRUE(source->groups[1] != nullptr);
      const auto& group = *source->groups[1];
      // A group with only children must not record geometry: that is what triggers the lazy
      // fallback.
      EXPECT_TRUE(group.geometry == nullptr)
      << "a group with only children must not record geometry";
      ASSERT_TRUE(group.contour != nullptr); ASSERT_TRUE(group.contour->image() != nullptr);
      ASSERT_TRUE(group.content != nullptr)
      << "control: the non-Geometry style in this bucket records the shared content";
      EXPECT_EQ(ResolveContentSource(group, glass.get()), group.contour)
      << "Glass must resolve to the shared contour when no geometry exists";)
}

// The merged-mask contract: a fill area and its outside stroke band are merged into a single shape
// grown by the stroke width (size += 2w, roundness += w). Two adjacent opaque regions anti-alias on
// their own curves and their coverages add up to less than one on the corner arcs, so the single
// merged region has to reach full coverage everywhere inside its outer boundary. The merged region is
// rendered and its coverage is read back, and a spread shadow probes that its outline really is the
// grown rounded rectangle.
TGFX_TEST_PRIVATE(LayerTest, MaskFillAndStrokeUnionKeepsFullCoverageOnRoundedCorners) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const Rect rect =
      Rect::MakeXYWH(MaskProbeRectLeft, MaskProbeRectTop, MaskProbeRectSize, MaskProbeRectSize);
  struct Probe {
    float radius;
    float strokeWidth;
    StrokeAlign align;
  };
  // Radius, stroke width and alignment jointly decide the outer geometry the merged fill has to
  // reach; both sides of the point where the corner radius and the stroke width meet are covered.
  const Probe probes[] = {
      {24.0f, 8.0f, StrokeAlign::Center},  {24.0f, 8.0f, StrokeAlign::Inside},
      {24.0f, 8.0f, StrokeAlign::Outside}, {12.0f, 4.0f, StrokeAlign::Center},
      {12.0f, 4.0f, StrokeAlign::Inside},  {12.0f, 4.0f, StrokeAlign::Outside},
      {8.0f, 8.0f, StrokeAlign::Center},   {8.0f, 8.0f, StrokeAlign::Inside},
      {8.0f, 8.0f, StrokeAlign::Outside},  {2.0f, 8.0f, StrokeAlign::Center},
      {2.0f, 8.0f, StrokeAlign::Inside},   {2.0f, 8.0f, StrokeAlign::Outside},
      {4.0f, 2.0f, StrokeAlign::Center},   {4.0f, 2.0f, StrokeAlign::Inside},
      {4.0f, 2.0f, StrokeAlign::Outside},
  };
  constexpr float spread = 6.0f;
  constexpr float diagonal = 0.70710678f;

  for (const auto& probe : probes) {
    auto outward = MaskStrokeOutwardExtent(probe.strokeWidth, probe.align);
    auto outerRadius = probe.radius + outward;
    auto outerRect = rect;
    outerRect.outset(outward, outward);

    auto pixels = RenderMergedFillMask(context, probe.radius, outward);
    ASSERT_EQ(pixels.size(), static_cast<size_t>(MaskProbeSurfaceSize) * MaskProbeSurfaceSize)
        << "merged-fill mask raster, r=" << probe.radius << " w=" << probe.strokeWidth
        << " align=" << static_cast<int>(probe.align);
    auto coverage = ScanInteriorCoverage(pixels, outerRect, outerRadius);
    EXPECT_GT(coverage.interior, 0)
        << "merged-fill interior ROI, r=" << probe.radius << " w=" << probe.strokeWidth
        << " align=" << static_cast<int>(probe.align);
    EXPECT_EQ(coverage.belowFull, 0)
        << "merged-fill interior ROI, r=" << probe.radius << " w=" << probe.strokeWidth
        << " align=" << static_cast<int>(probe.align)
        << ": the merged region must reach full coverage inside its outer boundary: "
        << coverage.belowFull << " of " << coverage.interior
        << " interior pixels are below alpha 255, lowest " << coverage.lowest << " at ("
        << coverage.lowestX << ", " << coverage.lowestY << ")";

    // Spread-shadow probe: the merged outline is the grown rounded rectangle, so its shadow reaches
    // just outside the rounded corner and stops beyond the spread.
    auto shadowLayer = VectorLayer::Make();
    auto mergedRect = Rectangle::Make();
    mergedRect->setPosition({outerRect.centerX(), outerRect.centerY()});
    mergedRect->setSize({outerRect.width(), outerRect.height()});
    mergedRect->setRoundness(outerRadius);
    auto mergedGroup = VectorGroup::Make();
    mergedGroup->setElements({mergedRect, FillStyle::Make(SolidColor::Make(Color::White()))});
    shadowLayer->setContents({mergedGroup});
    auto shadow = DropShadowStyle::Make(0, 0, 0, 0, Color::Black());
    shadow->setSpread(spread);
    shadowLayer->setLayerStyles({shadow});
    const auto cornerCenterX = outerRect.left + outerRadius;
    const auto cornerCenterY = outerRect.top + outerRadius;
    auto probeOffset = (outerRadius + 2.0f) * diagonal;
    auto justOutside = RenderSpreadShadowPixel(
        context, shadowLayer, static_cast<int>(cornerCenterX - probeOffset + 0.5f),
        static_cast<int>(cornerCenterY - probeOffset + 0.5f));
    ASSERT_TRUE(justOutside.has_value()) << "merged-fill shadow probe, r=" << probe.radius;
    EXPECT_NE(*justOutside, 0u)
        << "merged-fill shadow corner ROI, r=" << probe.radius << " w=" << probe.strokeWidth
        << " align=" << static_cast<int>(probe.align)
        << ": the shadow must follow the merged rounded rectangle to its corner";
    auto farOffset = (outerRadius + spread + 4.0f) * diagonal;
    auto beyondSpread = RenderSpreadShadowPixel(context, shadowLayer,
                                                static_cast<int>(cornerCenterX - farOffset + 0.5f),
                                                static_cast<int>(cornerCenterY - farOffset + 0.5f));
    ASSERT_TRUE(beyondSpread.has_value()) << "merged-fill shadow probe, r=" << probe.radius;
    EXPECT_EQ(*beyondSpread, 0u)
        << "merged-fill beyond-spread ROI, r=" << probe.radius << " w=" << probe.strokeWidth
        << " align=" << static_cast<int>(probe.align)
        << ": the shadow must stop beyond the spread of the merged outline";
  }
}

// Renders one UDF-path glass panel over a horizontal ramp and returns the layer-space width of the
// refraction band on its left edge, measured as the inward reach of the differing samples at the
// vertical centre. The ramp turns a backdrop displacement of d layer pixels into a difference of d
// samples, so the reach is read straight from the difference profile.
static float MeasureLeftBandReach(Context* context, float zoom) {
  constexpr float shapeW = 200.0f;
  constexpr float shapeH = 120.0f;
  constexpr float offset = 30.0f;
  constexpr float rampSpan = 256.0f;
  const auto surfaceSize = static_cast<int>(std::ceil((offset * 2.0f + shapeW) * zoom)) + 4;
  auto makePanel = [&](bool withGlass) {
    auto layer = VectorLayer::Make();
    Path path = {};
    // Non-uniform corners keep the shape off the analytical SDF path, so this exercises the UDF band.
    path.addRRect(RRect::MakeRectXY(Rect::MakeWH(shapeW, shapeH), 40.0f, 20.0f));
    auto shape = ShapePath::Make();
    shape->setPath(std::move(path));
    auto group = VectorGroup::Make();
    group->setElements({shape, MakeZeroAlphaFill()});
    layer->setContents({group});
    layer->setMatrix(Matrix::MakeTrans(offset, offset));
    if (withGlass) {
      layer->setLayerStyles({GlassStyle::Make(80, 100, 0, 0, 0, 0, 0)});
    }
    return layer;
  };
  auto render = [&](bool withGlass) {
    auto surface = Surface::Make(context, surfaceSize, surfaceSize);
    auto displayList = std::make_unique<DisplayList>();
    if (zoom != 1.0f) {
      displayList->setZoomScale(zoom);
    }
    // A smooth horizontal ramp instead of the checker: a displacement of d layer pixels shifts the
    // sampled color proportionally, so the difference profile is the displacement profile.
    auto ramp = ShapeLayer::Make();
    Path rampPath = {};
    rampPath.addRect(
        Rect::MakeWH(static_cast<float>(surfaceSize), static_cast<float>(surfaceSize)));
    ramp->setPath(rampPath);
    auto rampShader = Shader::MakeLinearGradient({0.0f, 0.0f}, {rampSpan, 0.0f},
                                                 {Color::Black(), Color::White()});
    ramp->setFillStyle(ShapeStyle::Make(rampShader));
    displayList->root()->addChild(ramp);
    displayList->root()->addChild(makePanel(withGlass));
    displayList->render(surface.get());
    auto info =
        ImageInfo::Make(surfaceSize, surfaceSize, ColorType::RGBA_8888, AlphaType::Premultiplied);
    std::vector<uint32_t> pixels(static_cast<size_t>(surfaceSize) *
                                 static_cast<size_t>(surfaceSize));
    if (!surface->readPixels(info, pixels.data())) {
      return std::vector<uint32_t>{};
    }
    return pixels;
  };
  auto styled = render(true);
  auto plain = render(false);
  if (styled.empty() || plain.empty()) {
    return -1.0f;
  }
  auto differs = [&](int x, int y) {
    auto i = static_cast<size_t>(y) * static_cast<size_t>(surfaceSize) + static_cast<size_t>(x);
    int maxDiff = 0;
    for (int shift = 0; shift < 32; shift += 8) {
      maxDiff = std::max(maxDiff, std::abs(static_cast<int>((styled[i] >> shift) & 0xFF) -
                                           static_cast<int>((plain[i] >> shift) & 0xFF)));
    }
    return maxDiff;
  };
  const auto scanY = static_cast<int>((offset + shapeH * 0.5f) * zoom);
  const auto left = static_cast<int>(offset * zoom);
  const auto right = static_cast<int>((offset + shapeW) * zoom);
  const auto mid = (left + right) / 2;
  int leftBand = left;
  for (int x = left; x < mid; x++) {
    if (differs(x, scanY) > 2) {
      leftBand = x;
    }
  }
  return static_cast<float>(leftBand - left) / zoom;
}

TGFX_TEST(LayerTest, GlassUDFBandReachIsZoomInvariant) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  const float reachAtOne = MeasureLeftBandReach(context, 1.0f);
  const float reachAtFive = MeasureLeftBandReach(context, 5.0f);
  ASSERT_GT(reachAtOne, 0.0f);
  ASSERT_GT(reachAtFive, 0.0f);
  // The density cap keeps radius * density inside the tent box, so the layer-space band must not
  // shrink as the canvas zooms; without the cap the reach collapses at high zoom.
  EXPECT_NEAR(reachAtFive, reachAtOne, 3.0f)
      << "UDF band reach changed with zoom: " << reachAtOne << " -> " << reachAtFive;
}

TGFX_TEST(LayerTest, GlassCoverageKeepsTransparentColorRegion) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  const auto glass = GlassStyle::Make(0, 50, 100, 0, 0, 0, 0);
  const auto regionABounds = Rect::MakeLTRB(0.0f, 0.0f, 60.0f, 80.0f);
  const auto regionBBounds = Rect::MakeLTRB(80.0f, 0.0f, 140.0f, 80.0f);
  RegionSpec bareGeometry = {};
  bareGeometry.bounds = regionABounds;
  RegionSpec transparent = {};
  transparent.bounds = regionBBounds;
  transparent.fill = RegionFillKind::TransparentColor;
  auto plain = RenderPanelOverChecker(context, MakePanel({bareGeometry, transparent}));
  auto glassPanel =
      RenderPanelOverChecker(context, MakePanel({bareGeometry, transparent}, {glass}));
  ASSERT_TRUE(plain != nullptr && glassPanel != nullptr);
  // The geometry keeps every painter of the layer, so a transparent solid color sitting next to
  // bare geometry still gets its region covered.
  auto roiA = regionABounds;
  roiA.offset(GlassPanelLeft, GlassPanelTop);
  roiA.inset(5.0f, 5.0f);
  EXPECT_GT(CountDifferingPixels(plain.get(), glassPanel.get(), roiA, 16),
            static_cast<int>(roiA.width() * roiA.height()) / 5)
      << "bare-geometry region must stay covered";
  auto roiB = regionBBounds;
  roiB.offset(GlassPanelLeft, GlassPanelTop);
  roiB.inset(5.0f, 5.0f);
  EXPECT_GT(CountDifferingPixels(plain.get(), glassPanel.get(), roiB, 16),
            static_cast<int>(roiB.width() * roiB.height()) / 5)
      << "transparent-color region must be covered too";
}

// The coverage comes from the layer's geometry, not from what the paint renders: a gradient whose
// right half is fully transparent still gets covered there.
TGFX_TEST(LayerTest, GlassCoverageIgnoresGradientTransparency) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  const auto glass = GlassStyle::Make(0, 50, 100, 0, 0, 0, 0);
  const auto bounds = Rect::MakeLTRB(0.0f, 0.0f, 140.0f, 80.0f);
  RegionSpec gradient = {};
  gradient.bounds = bounds;
  gradient.fill = RegionFillKind::Gradient;
  auto plain = RenderPanelOverChecker(context, MakePanel({gradient}));
  auto glassPanel = RenderPanelOverChecker(context, MakePanel({gradient}, {glass}));
  ASSERT_TRUE(plain != nullptr && glassPanel != nullptr);
  // The opaque left half and the fully transparent right half must both be covered.
  for (const auto& half :
       {Rect::MakeLTRB(0.0f, 0.0f, 60.0f, 80.0f), Rect::MakeLTRB(80.0f, 0.0f, 140.0f, 80.0f)}) {
    auto roi = half;
    roi.offset(GlassPanelLeft, GlassPanelTop);
    roi.inset(5.0f, 5.0f);
    EXPECT_GT(CountDifferingPixels(plain.get(), glassPanel.get(), roi, 16),
              static_cast<int>(roi.width() * roi.height()) / 5)
        << "the gradient's stops are geometry too, so the glass must cover them";
  }
}

TGFX_TEST(LayerTest, GlassStyleBackgroundOutsets) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 800, 200);
  auto displayList = std::make_unique<DisplayList>();

  auto sharpLayer = SolidLayer::Make();
  sharpLayer->setWidth(150);
  sharpLayer->setHeight(100);
  sharpLayer->setLayerStyles({GlassStyle::Make(80, 50, 0, 50, 0, 0, 0)});
  displayList->root()->addChild(sharpLayer);

  auto softLayer = SolidLayer::Make();
  softLayer->setWidth(150);
  softLayer->setHeight(100);
  softLayer->setMatrix(Matrix::MakeTrans(200, 0));
  softLayer->setLayerStyles({GlassStyle::Make(0, 50, 100, 0, 0, 0, 0)});
  displayList->root()->addChild(softLayer);

  auto mixedLayer = SolidLayer::Make();
  mixedLayer->setWidth(150);
  mixedLayer->setHeight(100);
  mixedLayer->setMatrix(Matrix::MakeTrans(400, 0));
  mixedLayer->setLayerStyles(
      {BackgroundBlurStyle::Make(100, 100), GlassStyle::Make(80, 50, 0, 50, 0, 0, 0)});
  displayList->root()->addChild(mixedLayer);

  auto combinedLayer = SolidLayer::Make();
  combinedLayer->setWidth(150);
  combinedLayer->setHeight(100);
  combinedLayer->setMatrix(Matrix::MakeTrans(600, 0));
  combinedLayer->setLayerStyles({GlassStyle::Make(80, 50, 100, 50, 0, 0, 0)});
  displayList->root()->addChild(combinedLayer);

  displayList->render(surface.get());

  TGFX_PRIVATE_ACCESS(EXPECT_GT(sharpLayer->maxBackgroundOutset, 0.0f);
                      EXPECT_GT(sharpLayer->minBackgroundOutset, 0.0f);
                      EXPECT_GT(softLayer->minBackgroundOutset, 0.0f);
                      EXPECT_EQ(softLayer->maxBackgroundOutset, softLayer->minBackgroundOutset);
                      EXPECT_GT(mixedLayer->maxBackgroundOutset, 0.0f);
                      EXPECT_GT(mixedLayer->minBackgroundOutset, 0.0f); EXPECT_GT(
                          combinedLayer->maxBackgroundOutset, sharpLayer->maxBackgroundOutset);
                      EXPECT_GT(combinedLayer->maxBackgroundOutset, softLayer->maxBackgroundOutset);
                      EXPECT_GT(combinedLayer->minBackgroundOutset, 0.0f);)
}

TGFX_TEST(LayerTest, GlassStyleShapeLayerContentOffset) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 240, 220);
  DisplayList displayList;

  auto background = ImageLayer::Make();
  background->setImage(MakeImage("resources/apitest/checker_128.png"));
  background->setMatrix(Matrix::MakeScale(2));
  displayList.root()->addChild(background);

  Path glassPath;
  glassPath.addRRect(RRect::MakeRectXY(Rect::MakeXYWH(75, 75, 90, 70), 20, 20));
  EXPECT_EQ(glassPath.getBounds(), Rect::MakeXYWH(75, 75, 90, 70));

  auto glassLayer = ShapeLayer::Make();
  glassLayer->setPath(glassPath);
  glassLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 255, 255, 128)));
  glassLayer->setLayerStyles({GlassStyle::Make(80, 50, 0, 50, 0, 0, 0)});
  displayList.root()->addChild(glassLayer);

  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/GlassStyleShapeLayerContentOffset"));
}

TGFX_TEST(LayerTest, GlassStyleEllipse) {
  RunGlassStyleTest("Ellipse");
}

TGFX_TEST(LayerTest, GlassStyleZoom) {
  RunGlassStyleTest("Zoom", 2.0f);
}

TGFX_TEST(LayerTest, GlassStyleRoundedRect) {
  // Rounded rectangle with unequal width/height (180x120, radius 30x30) exercises the SDF path.
  RunGlassStyleTest("RoundedRect", 1.0f, 180.0f, 120.0f, 30.0f, 30.0f);
}

TGFX_TEST(LayerTest, GlassStyleSplayCompare) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  constexpr float cellSize = 200;
  constexpr float gap = 10;
  constexpr int cols = 2;
  int surfaceW = static_cast<int>(cols * (cellSize + gap) + gap);
  int surfaceH = static_cast<int>(cellSize + 2 * gap);
  auto surface = Surface::Make(context, surfaceW, surfaceH);
  auto displayList = std::make_unique<DisplayList>();
  auto bgImage = MakeImage("resources/apitest/checker_128.png");
  // Square glass panel (180x180, sharp corners), refraction=100 and depth=50. Left cell uses
  // splay=1, right cell uses splay=2, to compare the refraction direction blend at near-zero splay
  // values.
  AddGlassCell(displayList->root(), bgImage, gap, gap, cellSize, 100, 50, 0, 0, 1, 135, 50, 180.0f,
               180.0f, 0.0f, 0.0f);
  AddGlassCell(displayList->root(), bgImage, gap + cellSize + gap, gap, cellSize, 100, 50, 0, 0, 2,
               135, 50, 180.0f, 180.0f, 0.0f, 0.0f);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/GlassStyleSplayCompare"));
}

TGFX_TEST(LayerTest, GlassStyleShallowDepthEdgeLight) {
  // On a sharp square (cornerRadius = 0) the analytical SDF gradient degenerates along the corner
  // diagonal, where the highlight used to be trapped inside the gradientLength > 0.000001 gate.
  // That only becomes visible when a degenerate sample also lands inside the edge-light band, i.e.
  // when r_eff (= glassThickness = depth) <= edgeDist <= band, with the band floor being
  // max(1.5, 1 / contentScale) layer pixels. The degenerate set is |px| == |py| held exactly in
  // float and |px| <= halfW - r_eff, so a hit needs both the band window and the grid phase.
  // Cells 2, 4 and 6 hit that window; cells 1, 3 and 5 are controls. Cell 1 has the same depth and
  // band window as cell 2 but a 180 px panel, whose sampling phase keeps |px| == |py| from holding
  // exactly, so it stays unchanged. Cell 3 (depth 5) sits outside the band window, and cell 5
  // (cornerRadius 24 pushes r_eff to 24) sits far outside the band. Cells 4 and 6 draw a 100 px and
  // a 180 px panel at 0.5 scale, so contentScale becomes 0.5 and the band widens to 2 layer pixels
  // while the corner samples stay inside the window.
  //
  // Phase note: a hit needs the degenerate set to meet the band window, so it depends on both the
  // window and the sampling grid; no configuration hits independently of the phase. Resizing a cell
  // moves the phase without changing the mechanism, so a cell can drop out of the window. To
  // re-verify, temporarily move the light response back inside the gradient gate and re-render
  // this sheet: cells 2, 4 and 6 should differ again. If they do not, the phase changed, so pick
  // another panel size whose samples land on the diagonal.
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  constexpr float cellSize = 200;
  constexpr float gap = 10;
  constexpr int cols = 6;
  int surfaceW = static_cast<int>(cols * (cellSize + gap) + gap);
  int surfaceH = static_cast<int>(cellSize + 2 * gap);
  auto surface = Surface::Make(context, surfaceW, surfaceH);
  auto displayList = std::make_unique<DisplayList>();
  auto bgImage = MakeImage("resources/apitest/checker_128.png");
  // cell 1: control, sharp 180 px panel at depth 0.5. It shares cell 2's depth and band window, but
  // its sampling phase keeps |px| == |py| from holding exactly, so nothing changes.
  AddGlassCell(displayList->root(), bgImage, gap, gap, cellSize, 80, 0.5f, 0, 0, 50, 0, 100, 180.0f,
               180.0f, 0.0f, 0.0f);
  // cell 2: positive, the same at a 100 px panel whose diagonal sample phase lands in the band -> 4
  // corner pixels brighten.
  AddGlassCell(displayList->root(), bgImage, gap + (cellSize + gap), gap, cellSize, 80, 0.5f, 0, 0,
               50, 0, 100, 100.0f, 100.0f, 0.0f, 0.0f);
  // cell 3: control, depth far outside the band window.
  AddGlassCell(displayList->root(), bgImage, gap + 2.0f * (cellSize + gap), gap, cellSize, 80, 5.0f,
               0, 0, 50, 0, 100, 180.0f, 180.0f, 0.0f, 0.0f);
  // cell 4: positive, the 100 px panel drawn at 0.5 scale (reviewer case): the band widens to 2
  // layer pixels and 2 corner pixels change.
  AddGlassCell(displayList->root(), bgImage, gap + 3.0f * (cellSize + gap), gap, cellSize, 80, 1.0f,
               0, 0, 50, 0, 100, 100.0f, 100.0f, 0.0f, 0.0f, Color::FromRGBA(0, 100, 255, 255),
               0.5f);
  // cell 5: control, rounded corner (r_eff = 24) keeps the degenerate set out of the band.
  AddGlassCell(displayList->root(), bgImage, gap + 4.0f * (cellSize + gap), gap, cellSize, 80, 0.5f,
               0, 0, 50, 0, 100, 180.0f, 180.0f, 24.0f, 24.0f);
  // cell 6: positive, the 180 px panel at 0.5 scale: the band widens to 2 layer pixels and 2 corner
  // pixels change.
  AddGlassCell(displayList->root(), bgImage, gap + 5.0f * (cellSize + gap), gap, cellSize, 80, 0.5f,
               0, 0, 50, 0, 100, 180.0f, 180.0f, 0.0f, 0.0f, Color::FromRGBA(0, 100, 255, 255),
               0.5f);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/GlassStyleShallowDepthEdgeLight"));
}

TGFX_TEST(LayerTest, GlassStyleEllipseSDF) {
  // Ellipse with unequal width/height (180x120, radius = half dimensions) exercises the SDF path.
  RunGlassStyleTest("EllipseSDF", 1.0f, 180.0f, 120.0f, 90.0f, 60.0f);
}

TGFX_TEST(LayerTest, GlassStyleEllipticalCorner) {
  // Rounded rect with elliptical corners (radius 40x20, rx != ry) falls back to the AlphaMask
  // path because the SDF shader only supports uniform circular corners (rx == ry).
  RunGlassStyleTest("EllipticalCorner", 1.0f, 180.0f, 120.0f, 40.0f, 20.0f);
}

TGFX_TEST(LayerTest, GlassStyleEllipticalCornerSingleCell) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  constexpr float cellSize = 200;
  auto surface = Surface::Make(context, static_cast<int>(cellSize), static_cast<int>(cellSize));
  auto displayList = std::make_unique<DisplayList>();
  auto bgImage = MakeImage("resources/apitest/checker_128.png");
  AddGlassCell(displayList->root(), bgImage, 0, 0, cellSize, 50, 100, 0, 0, 50, 135, 50, 180.0f,
               120.0f, 40.0f, 20.0f);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/GlassStyleEllipticalCornerSingleCell"));
}

TGFX_TEST(LayerTest, GlassStyleExtremeAspectRatioUDF) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 700, 180);
  auto displayList = std::make_unique<DisplayList>();
  auto bgImage = MakeImage("resources/apitest/checker_128.png");

  // Single background layer spanning the full surface.
  auto imgLayer = ImageLayer::Make();
  imgLayer->setImage(bgImage);
  auto scale = 700.0f / static_cast<float>(std::max(bgImage->width(), bgImage->height()));
  imgLayer->setMatrix(Matrix::MakeScale(scale, scale));
  displayList->root()->addChild(imgLayer);

  // Colored shapes for refraction contrast.
  auto blueRect = ShapeLayer::Make();
  Path bluePath = {};
  bluePath.addRect(Rect::MakeXYWH(50, 30, 245, 60));
  blueRect->setPath(bluePath);
  blueRect->setFillStyle(ShapeStyle::Make(Color::FromRGBA(0, 100, 255, 255)));
  displayList->root()->addChild(blueRect);

  auto greenCircle = ShapeLayer::Make();
  Path greenPath = {};
  greenPath.addOval(Rect::MakeXYWH(400, 100, 280, 60));
  greenCircle->setPath(greenPath);
  greenCircle->setFillStyle(ShapeStyle::Make(Color::FromRGBA(50, 200, 80, 200)));
  displayList->root()->addChild(greenCircle);

  // Three glass panels with increasing widths, all using the UDF path (rx != ry).
  float glassWidths[] = {200, 400, 600};
  for (int i = 0; i < 3; i++) {
    float gw = glassWidths[i];
    float gh = 20;
    auto glassLayer = SolidLayer::Make();
    glassLayer->setColor(Color::FromRGBA(255, 255, 255, 128));
    glassLayer->setWidth(gw);
    glassLayer->setHeight(gh);
    glassLayer->setRadiusX(20);
    glassLayer->setRadiusY(10);
    float glassX = (700.0f - gw) * 0.5f;
    float glassY = 20.0f + static_cast<float>(i) * 60.0f;
    glassLayer->setMatrix(Matrix::MakeTrans(glassX, glassY));
    auto style = GlassStyle::Make(80, 70, 0, 50, 50, 135, 50);
    glassLayer->setLayerStyles({style});
    displayList->root()->addChild(glassLayer);
  }

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/GlassStyleExtremeAspectRatioUDF"));
}

static std::shared_ptr<SolidLayer> BuildClippedGlassEvaluationScene(DisplayList* displayList) {
  auto background = ImageLayer::Make();
  background->setImage(MakeImage("resources/apitest/checker_128.png"));
  background->setMatrix(Matrix::MakeScale(5));
  displayList->root()->addChild(background);

  auto accent = SolidLayer::Make();
  accent->setColor(Color::FromRGBA(230, 50, 40, 255));
  accent->setWidth(60);
  accent->setHeight(60);
  accent->setMatrix(Matrix::MakeTrans(260, 270));
  displayList->root()->addChild(accent);

  constexpr float PanelWidth = 500;
  constexpr float PanelHeight = 150;
  constexpr float GlassScale = 8;
  for (int index = 0; index < 3; ++index) {
    auto viewport = Layer::Make();
    viewport->setScrollRect(Rect::MakeWH(PanelWidth, PanelHeight));
    viewport->setMatrix(Matrix::MakeTrans(50, 50 + static_cast<float>(index) * 175));

    auto glass = SolidLayer::Make();
    glass->setColor(Color::FromRGBA(255, 255, 255, 128));
    glass->setWidth(1000);
    glass->setHeight(300);
    glass->setMatrix(index == 0 ? Matrix::MakeScale(GlassScale, 2.0f)
                                : Matrix::MakeScale(GlassScale));
    if (index == 1) {
      glass->setRadiusX(40);
      glass->setRadiusY(20);
      glass->setLayerStyles({GlassStyle::Make(0, 30, 20, 0, 70, 135, 0)});
    } else if (index == 2) {
      glass->setRadiusX(40);
      glass->setRadiusY(40);
      glass->setLayerStyles({GlassStyle::Make(100, 100, 100, 100, 50, 135, 80)});
    } else {
      glass->setRadiusX(40);
      glass->setRadiusY(40);
      glass->setLayerStyles({GlassStyle::Make(60, 30, 20, 20, 30, 135, 80)});
    }
    viewport->addChild(glass);
    displayList->root()->addChild(viewport);
  }
  return accent;
}

TGFX_TEST(LayerTest, GlassStyleClippedEvaluationDirect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 600, 600);
  ASSERT_TRUE(surface != nullptr);
  DisplayList displayList;
  displayList.setRenderMode(RenderMode::Direct);
  BuildClippedGlassEvaluationScene(&displayList);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/GlassStyleClippedEvaluationDirect"));
}

TGFX_TEST(LayerTest, GlassStyleClippedEvaluationTiled) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 600, 600);
  ASSERT_TRUE(surface != nullptr);
  DisplayList displayList;
  displayList.setRenderMode(RenderMode::Tiled);
  displayList.setTileSize(128);
  auto accent = BuildClippedGlassEvaluationScene(&displayList);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/GlassStyleClippedEvaluationTiled"));

  accent->setColor(Color::FromRGBA(40, 90, 230, 255));
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/GlassStyleClippedEvaluationTiledUpdate"));
}

constexpr int DensityFloorSize = 2000;
constexpr int DensityOutputWidth = 1200;
constexpr int DensityOutputHeight = 1000;
constexpr int DensityFrameInset = 50;
constexpr float DensityPerspectiveDepth = 500.0f;
constexpr float DensityFloorScale = 0.2f;
constexpr float DensityPartiallyClippedFloorScaleX = 0.8f;
constexpr float DensityTopEdgeZ = 10.0f;
constexpr float DensityBottomEdgeZ = 210.0f;
constexpr float DensityFloorRotationDegrees = 30.0f;
constexpr int DensityGridStep = 8;
constexpr int DensityBackgroundValue = 24;
constexpr int DensityFrameBackgroundValue = 8;

static std::shared_ptr<Image> MakeDensityFloorImage(Context* context) {
  auto surface = Surface::Make(context, DensityFloorSize, DensityFloorSize);
  if (surface == nullptr) {
    return nullptr;
  }
  auto* canvas = surface->getCanvas();
  canvas->clear(Color::FromRGBA(12, 12, 12, 255));

  Paint gridPaint;
  gridPaint.setColor(Color::FromRGBA(180, 180, 180, 255));
  gridPaint.setStrokeWidth(1.0f);
  for (int coordinate = 0; coordinate <= DensityFloorSize; coordinate += DensityGridStep) {
    canvas->drawLine(static_cast<float>(coordinate), 0, static_cast<float>(coordinate),
                     DensityFloorSize, gridPaint);
    canvas->drawLine(0, static_cast<float>(coordinate), DensityFloorSize,
                     static_cast<float>(coordinate), gridPaint);
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
  for (int y = 64; y < DensityFloorSize; y += 160) {
    canvas->drawTextBlob(label, 1000, static_cast<float>(y), textPaint);
  }
  return surface->makeImageSnapshot();
}

static Matrix3D MakeDensityFloorMatrix(float horizontalScale) {
  const float anchorX = static_cast<float>(DensityFloorSize) * 0.5f;
  const auto offsetToAnchor = Matrix3D::MakeTranslate(-anchorX, 0, 0);
  const auto scale = Matrix3D::MakeScale(horizontalScale, DensityFloorScale, 1.0f);
  const auto rotation = Matrix3D::MakeRotate({1, 0, 0}, DensityFloorRotationDegrees);
  const auto translateZ = Matrix3D::MakeTranslate(0, 0, DensityTopEdgeZ);
  auto perspectiveMatrix = Matrix3D::I();
  perspectiveMatrix.setRowColumn(3, 2, -1.0f / DensityPerspectiveDepth);
  const auto originTranslate = Matrix3D::MakeTranslate(DensityOutputWidth * 0.5f, 200, 0);
  return originTranslate * perspectiveMatrix * translateZ * rotation * scale * offsetToAnchor;
}

static std::shared_ptr<Surface> RenderDensityFloor(Context* context,
                                                   const std::shared_ptr<Image>& image,
                                                   float horizontalScale) {
  auto surface = Surface::Make(context, DensityOutputWidth, DensityOutputHeight);
  if (surface == nullptr) {
    return nullptr;
  }
  surface->getCanvas()->clear(
      Color::FromRGBA(DensityBackgroundValue, DensityBackgroundValue, DensityBackgroundValue, 255));

  auto container = Layer::Make();
  container->setPreserve3D(true);
  auto floor = ImageLayer::Make();
  floor->setImage(image);
  floor->setMatrix3D(MakeDensityFloorMatrix(horizontalScale));
  container->addChild(floor);

  auto displayList = std::make_unique<DisplayList>();
  displayList->root()->addChild(container);
  displayList->render(surface.get());
  return surface;
}

static std::shared_ptr<Surface> RenderFramedDensityFloor(Context* context,
                                                         const std::shared_ptr<Image>& image,
                                                         float horizontalScale) {
  auto floorSurface = RenderDensityFloor(context, image, horizontalScale);
  if (floorSurface == nullptr) {
    return nullptr;
  }
  auto surface = Surface::Make(context, DensityOutputWidth + DensityFrameInset * 2,
                               DensityOutputHeight + DensityFrameInset * 2);
  if (surface == nullptr) {
    return nullptr;
  }
  auto floorImage = floorSurface->makeImageSnapshot();
  if (floorImage == nullptr) {
    return nullptr;
  }
  auto* canvas = surface->getCanvas();
  canvas->clear(Color::FromRGBA(DensityFrameBackgroundValue, DensityFrameBackgroundValue,
                                DensityFrameBackgroundValue, 255));
  canvas->drawImage(floorImage, DensityFrameInset, DensityFrameInset);
  return surface;
}

// A fully visible preserve-3D floor spanning z=10 (upper edge) to z=210 (lower edge). Its
// complete footprint stays inside the compositor viewport, verifying the contentScale density path.
TGFX_TEST(LayerTest, PerspectiveFloor10To210) {
  EXPECT_FLOAT_EQ(DensityTopEdgeZ + DensityFloorSize * DensityFloorScale * 0.5f,
                  DensityBottomEdgeZ);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeDensityFloorImage(context);
  ASSERT_TRUE(image != nullptr);

  auto surface = RenderDensityFloor(context, image, DensityFloorScale);
  ASSERT_TRUE(surface != nullptr);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PerspectiveFloor10To210"));
}

// A wider floor keeps the same z range but exceeds the compositor viewport horizontally. The
// clipped footprint exercises the projected dest/local density path, while the outer 50px frame
// keeps the final screenshot centered.
TGFX_TEST(LayerTest, PerspectiveFloorPartiallyClipped) {
  EXPECT_FLOAT_EQ(DensityTopEdgeZ + DensityFloorSize * DensityFloorScale * 0.5f,
                  DensityBottomEdgeZ);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeDensityFloorImage(context);
  ASSERT_TRUE(image != nullptr);

  auto surface = RenderFramedDensityFloor(context, image, DensityPartiallyClippedFloorScaleX);
  ASSERT_TRUE(surface != nullptr);
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/PerspectiveFloorPartiallyClipped"));
}

static Matrix3D MakeLocalToCompositorMatrix(const Matrix3D& nodeTransform, float contentScale,
                                            const Rect& renderRect) {
  Matrix3D matrix = nodeTransform;
  matrix.postScale(contentScale, contentScale, 1.0f);
  matrix.postTranslate(-renderRect.left, -renderRect.top, 0);
  return matrix;
}

static Matrix3D MakeRasterPerspectiveMatrix(float depth) {
  Matrix3D matrix = Matrix3D::I();
  matrix.setRowColumn(3, 2, -1.0f / depth);
  return matrix;
}

// A fully visible leaf retains contentScale density even when its projected scale differs from
// contentScale, exercising the coversWholeLeaf branch.
TGFX_TEST_PRIVATE(LayerTest, FlatLeaf_PreservesContentScaleSizing) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(100, 100);
    const float contentScale = 3.0f;
    const Rect renderRect = Rect::MakeWH(600, 600);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor = MakeLocalToCompositorMatrix(
        Matrix3D::MakeScale(0.5f, 0.5f, 1.0f), contentScale, renderRect);
    Render3DContext::RasterInfo info;

    ASSERT_TRUE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                   contentScale, &info));
    EXPECT_EQ(info.visibleLocal, localBounds);
    EXPECT_FLOAT_EQ(info.density.getScaleX(), contentScale);
    EXPECT_FLOAT_EQ(info.density.getScaleY(), contentScale);
    EXPECT_EQ(info.rasterWidth, 300);
    EXPECT_EQ(info.rasterHeight, 300);
  });
}

// An identity affine mapping rasters 1:1 and samples 1:1, so the offscreen mip chain can be
// skipped entirely.
TGFX_TEST_PRIVATE(LayerTest, AffineLeafWithoutMinification_SkipsMipmaps) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(100, 100);
    const float contentScale = 1.0f;
    const Rect renderRect = Rect::MakeWH(1000, 1000);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor =
        MakeLocalToCompositorMatrix(Matrix3D::I(), contentScale, renderRect);
    Render3DContext::RasterInfo info;

    ASSERT_TRUE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                   contentScale, &info));
    EXPECT_FALSE(info.mipmapped);
  });
}

// A uniformly minified leaf samples its raster at half size, so the mip chain must be retained.
TGFX_TEST_PRIVATE(LayerTest, MinifiedAffineLeaf_KeepsMipmaps) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(100, 100);
    const float contentScale = 1.0f;
    const Rect renderRect = Rect::MakeWH(1000, 1000);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor = MakeLocalToCompositorMatrix(
        Matrix3D::MakeScale(0.5f, 0.5f, 1.0f), contentScale, renderRect);
    Render3DContext::RasterInfo info;
    info.mipmapped = false;

    ASSERT_TRUE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                   contentScale, &info));
    EXPECT_TRUE(info.mipmapped);
  });
}

// A clipped leaf rasters at the projected density instead of contentScale, so even a minifying
// transform samples its visible footprint 1:1 and the mip chain can be skipped.
TGFX_TEST_PRIVATE(LayerTest, MinifiedClippedAffineLeaf_SkipsMipmaps) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(3000, 3000);
    const float contentScale = 1.0f;
    const Rect renderRect = Rect::MakeWH(1000, 1000);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor = MakeLocalToCompositorMatrix(
        Matrix3D::MakeScale(0.5f, 0.5f, 1.0f), contentScale, renderRect);
    Render3DContext::RasterInfo info;

    ASSERT_TRUE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                   contentScale, &info));
    EXPECT_LT(info.visibleLocal.width(), localBounds.width());
    EXPECT_FALSE(info.mipmapped);
  });
}

// The horizontal axis is extreme enough to overflow float-only arithmetic, but the double
// intermediates still resolve the exact decision: the vertical axis stays 1:1, so no minification
// occurs and the mip chain can be skipped.
TGFX_TEST_PRIVATE(LayerTest, ExtremeAnisotropicAffineLeaf_SkipsMipmaps) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(100, 100);
    const float contentScale = 1.0f;
    const Rect renderRect = Rect::MakeWH(1000, 1000);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor = MakeLocalToCompositorMatrix(
        Matrix3D::MakeScale(1.0e35f, 1.0f, 1.0f), contentScale, renderRect);
    Render3DContext::RasterInfo info;

    ASSERT_TRUE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                   contentScale, &info));
    EXPECT_FALSE(info.mipmapped);
  });
}

// A fully visible anisotropic mapping magnifies one axis 9x but minifies the other 0.11x, so the
// short axis still shrinks while sampling and the mip chain must be retained.
TGFX_TEST_PRIVATE(LayerTest, AnisotropicMinifiedAffineLeaf_KeepsMipmaps) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(100, 100);
    const float contentScale = 1.0f;
    const Rect renderRect = Rect::MakeWH(1000, 1000);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor = MakeLocalToCompositorMatrix(
        Matrix3D::MakeScale(9.0f, 0.11f, 1.0f), contentScale, renderRect);
    Render3DContext::RasterInfo info;
    info.mipmapped = false;

    ASSERT_TRUE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                   contentScale, &info));
    EXPECT_TRUE(info.mipmapped);
  });
}

// A large leaf under an extreme content scale is cropped to the compositor viewport before
// rasterization, avoiding an allocation of more than one million pixels per axis.
TGFX_TEST_PRIVATE(LayerTest, ExtremeZoom_CropsToViewportScale) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(1556, 1556);
    const float contentScale = 783.0f;
    const Rect renderRect = Rect::MakeWH(1861, 1861);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor =
        MakeLocalToCompositorMatrix(Matrix3D::I(), contentScale, renderRect);
    Render3DContext::RasterInfo info;

    ASSERT_TRUE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                   contentScale, &info));
    EXPECT_LT(info.visibleLocal.width(), localBounds.width());
    EXPECT_LT(info.visibleLocal.height(), localBounds.height());
    EXPECT_LE(static_cast<float>(info.rasterWidth), viewport.width() + 1.0f);
    EXPECT_LE(static_cast<float>(info.rasterHeight), viewport.height() + 1.0f);
  });
}

// A fully visible oversized leaf retains contentScale sizing and can still exceed the GPU texture
// limit because no viewport clipping is needed.
TGFX_TEST_PRIVATE(LayerTest, FullyVisibleOversizedLeaf_PreservesMainBehavior) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(200, 200);
    const float contentScale = 100.0f;
    const Rect renderRect = Rect::MakeWH(30000, 30000);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor =
        MakeLocalToCompositorMatrix(Matrix3D::I(), contentScale, renderRect);
    Render3DContext::RasterInfo info;

    ASSERT_TRUE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                   contentScale, &info));
    EXPECT_EQ(info.visibleLocal, localBounds);
    EXPECT_FLOAT_EQ(info.density.getScaleX(), contentScale);
    EXPECT_FLOAT_EQ(info.density.getScaleY(), contentScale);
    EXPECT_EQ(info.rasterWidth, 20000);
    EXPECT_EQ(info.rasterHeight, 20000);
  });
}

// A leaf that crosses the near plane is not rasterized because ComputeRasterInfo rejects every
// leaf with a corner behind the camera before computing visible footprints.
TGFX_TEST_PRIVATE(LayerTest, NearPlaneStraddle_CullsLeaf) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(100, 300);
    Matrix3D nodeTransform = Matrix3D::I();
    nodeTransform.setRowColumn(3, 1, -1.0f / 200.0f);
    const float contentScale = 1.0f;
    const Rect renderRect = Rect::MakeWH(1000, 1000);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor =
        MakeLocalToCompositorMatrix(nodeTransform, contentScale, renderRect);
    Render3DContext::RasterInfo info;

    EXPECT_FALSE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                    contentScale, &info));
  });
}

TGFX_TEST_PRIVATE(LayerTest, LeafOutsideViewport_CullsLeaf) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(200, 200);
    const float contentScale = 1.0f;
    const Rect renderRect = Rect::MakeWH(500, 500);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor = MakeLocalToCompositorMatrix(
        Matrix3D::MakeTranslate(10000, 10000, 0), contentScale, renderRect);
    Render3DContext::RasterInfo info;

    EXPECT_FALSE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                    contentScale, &info));
  });
}

TGFX_TEST_PRIVATE(LayerTest, LeafBehindCamera_CullsLeaf) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(200, 200);
    Matrix3D nodeTransform = Matrix3D::MakeTranslate(0, 0, 100);
    nodeTransform = MakeRasterPerspectiveMatrix(50.0f) * nodeTransform;
    const float contentScale = 1.0f;
    const Rect renderRect = Rect::MakeWH(500, 500);
    const Rect viewport = Rect::MakeWH(renderRect.width(), renderRect.height());
    const auto localToCompositor =
        MakeLocalToCompositorMatrix(nodeTransform, contentScale, renderRect);
    Render3DContext::RasterInfo info;

    EXPECT_FALSE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                    contentScale, &info));
  });
}

// A leaf clipped only along the horizontal axis keeps the untouched vertical range and derives a
// projected-footprint density distinct from the horizontal one.
TGFX_TEST_PRIVATE(LayerTest, ClippedAnisotropicProjection_UsesProjectedDensity) {
  TGFX_PRIVATE_ACCESS({
    const Rect localBounds = Rect::MakeWH(200, 200);
    Matrix3D nodeTransform = Matrix3D::MakeRotate({0, 1, 0}, 45.0f);
    nodeTransform = MakeRasterPerspectiveMatrix(200.0f) * nodeTransform;
    const float contentScale = 5.0f;
    const Rect renderRect = Rect::MakeWH(2000, 2000);
    // The leaf projects to x in [0, 414] and y in [0, 1000]. This viewport removes both
    // horizontal edges while preserving the complete vertical range.
    const Rect viewport = Rect::MakeLTRB(100, 0, 400, 2000);
    const auto localToCompositor =
        MakeLocalToCompositorMatrix(nodeTransform, contentScale, renderRect);
    Render3DContext::RasterInfo info;

    ASSERT_TRUE(Render3DContext::ComputeRasterInfo(localToCompositor, localBounds, viewport,
                                                   contentScale, &info));
    EXPECT_GT(info.visibleLocal.left, localBounds.left);
    EXPECT_LT(info.visibleLocal.right, localBounds.right);
    EXPECT_EQ(info.visibleLocal.height(), localBounds.height());

    EXPECT_NE(info.density.getScaleX(), info.density.getScaleY());
    EXPECT_LT(info.density.getTranslateX(), 0.0f);
    EXPECT_FLOAT_EQ(info.density.getTranslateY(), 0.0f);
    EXPECT_LE(static_cast<float>(info.rasterWidth), viewport.width() + 1.0f);
    EXPECT_LE(static_cast<float>(info.rasterHeight), viewport.height() + 1.0f);
  });
}

constexpr float FootprintEpsilon = 0.01f;

static Matrix3D MakeFootprintPerspectiveMatrix(float depth) {
  Matrix3D matrix = Matrix3D::I();
  matrix.setRowColumn(3, 2, -1.0f / depth);
  return matrix;
}

// Near-plane straddle: the bottom corners are behind the camera while the top corners are in
// front. Homogeneous clipping keeps only the finite in-front portion, bounded by the viewport.
TGFX_TEST(LayerTest, ComputeVisibleFootprintsStraddleNearPlane) {
  const Rect localBounds = Rect::MakeWH(100, 300);
  const Rect destRect = Rect::MakeWH(1000, 1000);
  Matrix3D matrix = Matrix3D::I();
  // W = 1 - y / 200. The top corners have W = 1; the bottom corners have W = -0.5.
  matrix.setRowColumn(3, 1, -1.0f / 200.0f);

  ASSERT_TRUE(Matrix3DUtils::IsRectBehindCamera(localBounds, matrix));
  EXPECT_GT(matrix.mapHomogeneous(0, 0, 0, 1).w, 0.0f);
  EXPECT_LT(matrix.mapHomogeneous(0, 300, 0, 1).w, 0.0f);

  Rect localFootprint;
  Rect destFootprint;
  ASSERT_TRUE(Matrix3DUtils::ComputeVisibleFootprints(localBounds, destRect, matrix,
                                                      &localFootprint, &destFootprint));
  // y = 500 / 3 projects to the viewport bottom (y / (1 - y / 200) = 1000).
  EXPECT_NEAR(localFootprint.left, 0, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.top, 0, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.right, 100, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.bottom, 500.0f / 3.0f, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.left, 0, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.top, 0, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.right, 600, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.bottom, 1000, FootprintEpsilon);
}

// Leaves fully behind the camera or fully outside the viewport have no surviving polygon.
TGFX_TEST(LayerTest, ComputeVisibleFootprintsCullsInvisibleLeaves) {
  const Rect localBounds = Rect::MakeWH(200, 200);
  const Rect destRect = Rect::MakeWH(600, 600);
  Rect localFootprint;
  Rect destFootprint;

  Matrix3D behindCamera = Matrix3D::MakeTranslate(0, 0, 100);
  behindCamera = MakeFootprintPerspectiveMatrix(50.0f) * behindCamera;
  EXPECT_FALSE(Matrix3DUtils::ComputeVisibleFootprints(localBounds, destRect, behindCamera,
                                                       &localFootprint, &destFootprint));

  const Matrix3D outsideViewport = Matrix3D::MakeTranslate(10000, 10000, 0);
  EXPECT_FALSE(Matrix3DUtils::ComputeVisibleFootprints(localBounds, destRect, outsideViewport,
                                                       &localFootprint, &destFootprint));
}

// Partial viewport overhang clamps the destination footprint and maps the same surviving region
// back to the corresponding local footprint.
TGFX_TEST(LayerTest, ComputeVisibleFootprintsClipsPartialViewportOverhang) {
  const Rect localBounds = Rect::MakeWH(100, 100);
  const Rect destRect = Rect::MakeWH(100, 100);
  // Leaf spans dest x in [50, 150], y in [0, 100]: the right half exceeds the viewport.
  const Matrix3D matrix = Matrix3D::MakeTranslate(50, 0, 0);

  Rect localFootprint;
  Rect destFootprint;
  ASSERT_TRUE(Matrix3DUtils::ComputeVisibleFootprints(localBounds, destRect, matrix,
                                                      &localFootprint, &destFootprint));
  EXPECT_NEAR(destFootprint.left, 50, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.right, 100, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.top, 0, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.bottom, 100, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.left, 0, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.right, 50, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.top, 0, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.bottom, 100, FootprintEpsilon);
}

// A fully visible projective leaf validates the local/destination footprint pairing. Both outputs
// describe the same local rect, with destination coordinates after perspective divide.
TGFX_TEST(LayerTest, ComputeVisibleFootprintsKeepsProjectiveFootprintsPaired) {
  const Rect localBounds = Rect::MakeWH(100, 100);
  const Rect destRect = Rect::MakeWH(200, 200);
  Matrix3D matrix = Matrix3D::I();
  // W = 1 - y / 400 stays positive over localBounds; the bottom-right corner (100, 100)
  // projects to (400/3, 400/3), setting both the destination right and bottom edges.
  matrix.setRowColumn(3, 1, -1.0f / 400.0f);

  Rect localFootprint;
  Rect destFootprint;
  ASSERT_TRUE(Matrix3DUtils::ComputeVisibleFootprints(localBounds, destRect, matrix,
                                                      &localFootprint, &destFootprint));
  EXPECT_NEAR(localFootprint.left, 0, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.top, 0, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.right, 100, FootprintEpsilon);
  EXPECT_NEAR(localFootprint.bottom, 100, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.left, 0, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.top, 0, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.right, 400.0f / 3.0f, FootprintEpsilon);
  EXPECT_NEAR(destFootprint.bottom, 400.0f / 3.0f, FootprintEpsilon);
}

// A singular local-to-destination homography cannot yield paired footprints.
TGFX_TEST(LayerTest, ComputeVisibleFootprintsRejectsSingularHomography) {
  const Rect localBounds = Rect::MakeWH(100, 100);
  const Rect destRect = Rect::MakeWH(200, 200);
  Matrix3D matrix = Matrix3D::I();
  matrix.setRowColumn(0, 0, 0);

  Rect localFootprint;
  Rect destFootprint;
  EXPECT_FALSE(Matrix3DUtils::ComputeVisibleFootprints(localBounds, destRect, matrix,
                                                       &localFootprint, &destFootprint));
}

// Tiled mode with per-frame tile budget. Zooming in invalidates every tile; with
// TileUpdateMode::Fast and setMaxTilesRefinedPerFrame(1), each render() call refines at most one
// missing tile, so every tile runs its own offscreen pass followed by a Picture::asImage unwrap.
// An anchor drift in the unwrap path then surfaces as block-aligned misalignment between tiles.
// The text is placed off-origin so the DrawImageRect subset offset is nonzero, which makes the
// unwrap anchor correction observable.
static void RenderNestedOffscreenTiledZoom(DisplayList* displayList,
                                           const std::shared_ptr<Surface>& surface, float zoomScale,
                                           const std::string& key) {
  displayList->setZoomScale(zoomScale);
  for (int i = 0; i < 40; ++i) {
    displayList->render(surface.get());
  }
  EXPECT_TRUE(Baseline::Compare(surface, key));
}

TGFX_TEST(LayerTest, NestedOffscreenTiledZoom) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 1024, 1024);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileUpdateMode(TileUpdateMode::Fast);
  displayList->setMaxTilesRefinedPerFrame(1);
  auto root = Layer::Make();
  displayList->root()->addChild(root);
  // Three nested layers each with passThroughBackground disabled, forcing every level through
  // its own offscreen render pass before compositing.
  Layer* parent = root.get();
  for (int i = 0; i < 3; ++i) {
    auto frame = Layer::Make();
    frame->setPassThroughBackground(false);
    parent->addChild(frame);
    parent = frame.get();
  }
  auto textLayer = TextLayer::Make();
  textLayer->setText("06");
  textLayer->setTextColor(Color::Black());
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  Font font(typeface, 40);
  textLayer->setFont(font);
  textLayer->setMatrix(Matrix::MakeTrans(30.0f, 20.0f));
  parent->addChild(textLayer);
  // Warm up at zoom=1.0 to build the initial tile cache and clear the startup dirty regions.
  displayList->render(surface.get());
  // Zoom in: the frame right after the zoom change has a zero tile budget, so every tile falls
  // back to the zoom=1.0 cache. Each following frame refines at most one missing tile. Loop until
  // all tiles in the viewport are rasterized at the target zoom.
  RenderNestedOffscreenTiledZoom(displayList.get(), surface, 12.29f,
                                 "LayerTest/NestedOffscreenTiledZoom");
}

TGFX_TEST(LayerTest, GlassStyleWithDropShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 250, 250);
  DisplayList displayList;

  auto background = SolidLayer::Make();
  background->setColor(Color::White());
  background->setWidth(150);
  background->setHeight(150);
  background->setMatrix(Matrix::MakeTrans(50, 50));
  displayList.root()->addChild(background);

  auto bottomRect = SolidLayer::Make();
  bottomRect->setColor(Color::FromRGBA(143, 0, 0));
  bottomRect->setWidth(100);
  bottomRect->setHeight(100);
  bottomRect->setMatrix(Matrix::MakeTrans(70, 70));
  displayList.root()->addChild(bottomRect);

  auto topRect = SolidLayer::Make();
  topRect->setColor(Color::FromRGBA(143, 0, 0, 26));
  topRect->setWidth(100);
  topRect->setHeight(100);
  topRect->setMatrix(Matrix::MakeTrans(70, 70));
  auto dropShadow = DropShadowStyle::Make(20, 20, 0, 0, Color::FromRGBA(0, 0, 0, 255));
  dropShadow->setShowBehindLayer(false);
  topRect->setLayerStyles({dropShadow, GlassStyle::Make(0, 50, 100, 0, 0, 0, 0)});
  displayList.root()->addChild(topRect);

  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerTest/GlassStyleWithDropShadow"));
}

// ===== A Glass on a parent whose geometry lives in child layers =====
//
// A parent layer carrying the Glass style whose geometry lives in child layers owns no painter at
// all, so it records no geometry and Glass falls back to the shared subtree contour, which is
// recording that keeps a 0%-alpha fill (see ResolveContentSource in LayerStyleSource.h). These cases
// pin the final RENDERED pixels over a checker backdrop under the frost-only and the refractive
// parameter sets.

// Full-parameter counterpart of MakeGeometryGlass for the light-carrying arms: the light angle and
// intensity survive, where MakeGeometryGlass drops them to 0.
static std::shared_ptr<GlassStyle> MakeGeometryGlassWithLight(float refraction, float depth,
                                                              float frost, float dispersion,
                                                              float splay, float lightAngle,
                                                              float lightIntensity) {
  auto glass =
      GlassStyle::Make(refraction, depth, frost, dispersion, splay, lightAngle, lightIntensity);
  glass->setExcludeChildEffects(true);
  return glass;
}

// One table over the parameter sets the parent/child cases are checked with: the frost-only path
// (refraction 0, so an interior difference is pure coverage), the refractive set (refraction 100 /
// depth 100 / frost 14 / dispersion 100) and a light-carrying set (refraction 80 / depth 20 / frost
// 4 / dispersion 50, light 45 at intensity 80). With refraction > 0 the glass DISPLACES the backdrop,
// so an interior region can differ for reasons other than coverage; every arm therefore pairs the
// interior reading with a no-fill control rendered under the SAME refractive glass, and with a donut
// base whose hole has to stay uncovered, plus a non-glass drop-shadow control.
TGFX_TEST_PRIVATE(LayerTest, GlassParentChildRenderedCoverage) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const Rect baseRect =
      Rect::MakeXYWH(MaskProbeRectLeft, MaskProbeRectTop, MaskProbeRectSize, MaskProbeRectSize);
  constexpr float ringWidth = 10.0f;
  const auto baseInteriorRoi = Rect::MakeLTRB(baseRect.left + 2.0f, baseRect.top + 2.0f,
                                              baseRect.right - 2.0f, baseRect.bottom - 2.0f);
  const auto interiorArea =
      static_cast<int>(baseInteriorRoi.width()) * static_cast<int>(baseInteriorRoi.height());
  const Rect holeRect = Rect::MakeXYWH(baseRect.left + 25.0f, baseRect.top + 25.0f,
                                       baseRect.width() - 50.0f, baseRect.height() - 50.0f);
  const auto holeRoi = Rect::MakeLTRB(holeRect.left + 2.0f, holeRect.top + 2.0f,
                                      holeRect.right - 2.0f, holeRect.bottom - 2.0f);
  const auto annulusTopRoi = Rect::MakeLTRB(baseRect.left + 2.0f, baseRect.top + 2.0f,
                                            baseRect.right - 2.0f, holeRect.top - 2.0f);
  // A region deep inside the base rectangle, used by the non-glass control: far enough from the ring
  // that a shadow driven by the ring alone cannot reach it.
  const auto deepInterior = Rect::MakeLTRB(baseRect.left + 20.0f, baseRect.top + 20.0f,
                                           baseRect.right - 20.0f, baseRect.bottom - 20.0f);

  auto renderDiff = [&](const std::shared_ptr<LayerStyle>& style, GlassChildFillKind fillKind,
                        const Rect& roi, int tolerance) {
    auto styled = RenderPanelOverChecker(
        context, MakeGlassChildPanel(fillKind, baseRect, holeRect, ringWidth, style));
    auto plain = RenderPanelOverChecker(
        context, MakeGlassChildPanel(fillKind, baseRect, holeRect, ringWidth, nullptr));
    if (styled == nullptr || plain == nullptr) {
      return -1;
    }
    return CountDifferingPixels(plain.get(), styled.get(), roi, tolerance);
  };

  struct Arm {
    const char* label;
    float refraction;
    float depth;
    float frost;
    float dispersion;
    float splay;
    float lightAngle;
    float lightIntensity;
  };
  const Arm arms[] = {
      {"frost-only 0/50/100/0/0 light 0@0", 0, 50, 100, 0, 0, 0, 0},
      {"refractive 100/100/14/100 light 0@0", 100, 100, 14, 100, 0, 0, 0},
      {"light 80/20/4/50 light 45@80", 80, 20, 4, 50, 0, 45, 80},
  };

  for (const auto& arm : arms) {
    auto glass = MakeGeometryGlassWithLight(arm.refraction, arm.depth, arm.frost, arm.dispersion,
                                            arm.splay, arm.lightAngle, arm.lightIntensity);
    // Core end-to-end proof: with the retained 0% fill the base interior has to light up.
    const auto interiorWithFill = renderDiff(glass, GlassChildFillKind::Full, baseInteriorRoi, 16);
    ASSERT_GE(interiorWithFill, 0) << "interior ROI render failed, " << arm.label;
    EXPECT_GT(interiorWithFill, interiorArea / 5)
        << "interior ROI (0% child fill, " << arm.label
        << "): the Glass must cover the previously invisible 0%-fill area and not just the ring: "
        << interiorWithFill << " of " << interiorArea << " interior pixels changed";

    // Control 1: without the fill painter the recordings hold the ring alone, so the interior stays
    // untouched under the SAME glass and the comparison above cannot come from displacement.
    const auto interiorNoFill = renderDiff(glass, GlassChildFillKind::None, baseInteriorRoi, 16);
    ASSERT_GE(interiorNoFill, 0) << "no-fill control render failed, " << arm.label;
    EXPECT_EQ(interiorNoFill, 0)
        << "interior ROI (no-fill control, " << arm.label
        << "): without the fill painter the Glass must stay ring-only and leave the interior "
           "untouched";

    // Control 2: an annulus base must keep its hole out of the coverage; the band reading proves the
    // hole reading is not vacuous.
    const auto annulusWithFill = renderDiff(glass, GlassChildFillKind::Donut, annulusTopRoi, 16);
    const auto holeWithFill = renderDiff(glass, GlassChildFillKind::Donut, holeRoi, 16);
    ASSERT_GE(annulusWithFill, 0) << "annulus band render failed, " << arm.label;
    ASSERT_GE(holeWithFill, 0) << "annulus hole render failed, " << arm.label;
    EXPECT_GT(annulusWithFill, 0)
        << "annulus band ROI (" << arm.label
        << "): the annulus band must be covered, otherwise the hole reading is vacuous";
    EXPECT_EQ(holeWithFill, 0)
        << "annulus hole ROI (" << arm.label
        << "): the annulus hole must stay uncovered, so the coverage follows the fill geometry and "
           "not its bounding box";

    // Control 3 (non-glass regression): a DropShadow consumes the shared content image, so a shadow
    // driven by the ring alone must leave the deep interior untouched.
    auto shadow = DropShadowStyle::Make(0, 0, 0, 0, Color::Black());
    auto shadowRing = renderDiff(
        shadow, GlassChildFillKind::Full,
        Rect::MakeLTRB(baseRect.left, baseRect.top - ringWidth, baseRect.right, baseRect.top), 2);
    const auto shadowInterior = renderDiff(shadow, GlassChildFillKind::Full, deepInterior, 16);
    ASSERT_GE(shadowRing, 0) << "drop-shadow ring render failed, " << arm.label;
    ASSERT_GE(shadowInterior, 0) << "drop-shadow interior render failed, " << arm.label;
    EXPECT_GT(shadowRing, 0)
        << "drop-shadow ring ROI (" << arm.label
        << "): the drop shadow must still render over the ring it does cover, or this control is "
           "vacuous";
    EXPECT_EQ(shadowInterior, 0)
        << "drop-shadow deep-interior ROI (" << arm.label
        << "): the drop shadow must keep using the content image (ring-only) and not the contour "
           "path";
  }
}

// ===== A collapsed fill geometry: the stroke band alone must not cover the interior =====

// The shape's extent comes from its own Rectangle element, not from a size declared on the layer: when
// that extent collapses, the rectangle-like fill geometry is built at 0x0, the sub-pixel fill is
// dropped and the Glass is left with the stroke band only. shareElement selects whether the fill and
// the stroke share one Rectangle element built from a single extent or use one each. The split arms
// keep the stroke geometry at the full extent while the fill collapses -- the most generous case for
// a stroke-only outline, which is exactly the "can the stroke alone cover the interior?" question (an
// outside stroke band is a ring).
static std::shared_ptr<Layer> MakeFrameSizeGlassPanel(float fillExtent, float strokeExtent,
                                                      bool shareElement, const Rect& rect,
                                                      float strokeWidth,
                                                      const std::shared_ptr<GlassStyle>& glass) {
  auto makeRect = [&rect](float extent) {
    auto shape = Rectangle::Make();
    shape->setPosition({rect.centerX(), rect.centerY()});
    shape->setSize({extent, extent});
    return shape;
  };
  // A retained 0%-alpha fill: style alpha 0 (paints nothing), color alpha 1 (still a visible color
  // source, so the geometry keeps feeding the shape classifiers).
  auto fill = FillStyle::Make(SolidColor::Make(Color::White()));
  fill->setAlpha(0.0f);
  // Translucent, so the Glass drawn under the band stays observable in a rendered comparison.
  auto stroke = StrokeStyle::Make(SolidColor::Make(Color::White()));
  stroke->setAlpha(0.4f);
  stroke->setStrokeWidth(strokeWidth);
  stroke->setStrokeAlign(StrokeAlign::Outside);
  stroke->setPlacement(LayerPlacement::Foreground);

  auto layer = VectorLayer::Make();
  if (shareElement) {
    auto geometry = makeRect(fillExtent);
    auto group = VectorGroup::Make();
    group->setElements({geometry, fill, stroke});
    layer->setContents({group});
  } else {
    auto fillGroup = VectorGroup::Make();
    fillGroup->setElements({makeRect(fillExtent), fill});
    auto strokeGroup = VectorGroup::Make();
    strokeGroup->setElements({makeRect(strokeExtent), stroke});
    layer->setContents({fillGroup, strokeGroup});
  }
  if (glass != nullptr) {
    layer->setLayerStyles({glass});
  }
  return layer;
}

// A proper-size fill covers the interior; a fill geometry that collapsed to 0 or below a pixel is
// dropped by the fill painter, leaving the outside stroke band as the only outline, so the Glass
// covers a ring and the interior stays untouched. Both readings are rendered over the checker.
TGFX_TEST_PRIVATE(LayerTest, CollapsedFillGeometryLeavesBandOnly) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const Rect rect =
      Rect::MakeXYWH(MaskProbeRectLeft, MaskProbeRectTop, MaskProbeRectSize, MaskProbeRectSize);
  constexpr float strokeWidth = 20.0f;
  auto outward = MaskStrokeOutwardExtent(strokeWidth, StrokeAlign::Outside);
  auto deepInterior = rect;
  deepInterior.inset(strokeWidth, strokeWidth);
  const auto deepArea =
      static_cast<int>(deepInterior.width()) * static_cast<int>(deepInterior.height());
  auto bandRoi = Rect::MakeLTRB(rect.left, rect.top - outward, rect.right, rect.top);

  struct SizeProbe {
    const char* label;
    float fillExtent;
    float strokeExtent;
    bool shareElement;
    bool collapsed;
  };
  // proper = the extent the shape carries (positive); zero-fill / subpixel-fill = that extent
  // collapsed while the stroke geometry still holds the full extent.
  const SizeProbe probes[] = {
      {"proper", MaskProbeRectSize, MaskProbeRectSize, true, false},
      {"zero-fill", 0.0f, MaskProbeRectSize, false, true},
      {"subpixel-fill", 0.005f, MaskProbeRectSize, false, true},
  };

  for (const auto& probe : probes) {
    const auto glass = MakeGeometryGlass(100, 100, 14, 100, 0);
    auto styled = RenderPanelOverChecker(
        context, MakeFrameSizeGlassPanel(probe.fillExtent, probe.strokeExtent, probe.shareElement,
                                         rect, strokeWidth, glass));
    auto plain = RenderPanelOverChecker(
        context, MakeFrameSizeGlassPanel(probe.fillExtent, probe.strokeExtent, probe.shareElement,
                                         rect, strokeWidth, nullptr));
    ASSERT_TRUE(styled != nullptr && plain != nullptr) << probe.label;
    const auto interiorDiff = CountDifferingPixels(plain.get(), styled.get(), deepInterior, 16);
    const auto bandDiff = CountDifferingPixels(plain.get(), styled.get(), bandRoi, 2);
    EXPECT_GT(bandDiff, 0)
        << "band ROI (" << probe.label
        << "): the outside stroke band must keep an outline and light up, otherwise the interior "
           "reading is vacuous";
    if (!probe.collapsed) {
      // Non-vacuity: the proper-size arm must be genuinely covered and genuinely refracted, so a
      // degenerate arm reading zero is meaningful.
      EXPECT_GT(interiorDiff, deepArea / 5)
          << "interior ROI (" << probe.label
          << "): the proper-size Frame must refract the whole interior: " << interiorDiff << " of "
          << deepArea << " interior pixels changed";
    } else {
      EXPECT_EQ(interiorDiff, 0)
          << "interior ROI (" << probe.label
          << "): with the fill geometry collapsed the Glass must not reach into the shape interior";
    }
  }
}

// ===== The geometry: bare geometry versus a capturing painter =====

// Size of the probe surface RasterizeGeometryAlpha reads the geometry onto.
static constexpr int GeometryProbeSize = 200;

// Reads the geometry coverage of `panel` onto a fixed probe surface, in layer-local coordinates,
// and returns its alpha raster. Returns an empty raster when the layer records no geometry.
static std::vector<int> RasterizeGeometryAlpha(Context* context,
                                               const std::shared_ptr<Layer>& panel) {
  std::vector<int> raster = {};
  if (panel == nullptr) {
    return raster;
  }
  DrawArgs drawArgs(context);
  std::shared_ptr<Image> image = nullptr;
  Point offset = Point::Zero();
  TGFX_PRIVATE_ACCESS(
      auto source = panel->getLayerStyleSource(drawArgs, Matrix::I());
      if (source == nullptr) { return raster; } for (auto& group
                                                     : source->groups) {
        if (group != nullptr && group->geometry != nullptr && group->geometry->image() != nullptr) {
          image = group->geometry->image();
          offset = group->geometry->imageOffset();
        }
      })
  if (image == nullptr) {
    return raster;
  }
  auto surface = Surface::Make(context, GeometryProbeSize, GeometryProbeSize);
  if (surface == nullptr) {
    return raster;
  }
  // The image is cropped to the outline's rounded-out bounds, so its recorded offset puts the
  // raster back into layer-local coordinates.
  surface->getCanvas()->drawImage(image, offset.x, offset.y);
  auto info = ImageInfo::Make(GeometryProbeSize, GeometryProbeSize, ColorType::RGBA_8888,
                              AlphaType::Premultiplied);
  std::vector<uint32_t> pixels(static_cast<size_t>(GeometryProbeSize) * GeometryProbeSize);
  if (!surface->readPixels(info, pixels.data())) {
    return raster;
  }
  raster.reserve(pixels.size());
  for (auto pixel : pixels) {
    raster.push_back(static_cast<int>((pixel >> 24) & 0xFF));
  }
  return raster;
}

// Counts the entries of `content` that carry no paint, i.e. geometries no painter captured. The unit
// is the entry, not the geometry: two bare rectangles recorded under one matrix batch into a single
// RectsContent and count as one, so a test needing two entries has to give them different matrices,
// as the Repeater case below does.
static int CountUnpaintedEntries(const LayerContent* content) {
  TGFX_PRIVATE_ACCESS(if (content != nullptr) {
    auto type = content->type();
    if (type == LayerContent::Type::Compose) {
      int count = 0;
      for (const auto& child : static_cast<const ComposeContent*>(content)->contents) {
        count += CountUnpaintedEntries(child.get());
      }
      return count;
    }
    if (type == LayerContent::Type::Matrix) {
      return CountUnpaintedEntries(static_cast<const MatrixContent*>(content)->content.get());
    }
    return static_cast<const GeometryContent*>(content)->hasPaint() ? 0 : 1;
  })
  // MSVC builds without TGFX_TEST_ACCESS_PRIVATE compile an empty body; every caller sits in a
  // TGFX_TEST_PRIVATE case that is not registered there.
  return 0;
}

static int CountUnpaintedEntries(const std::shared_ptr<Layer>& layer) {
  if (layer == nullptr) {
    return 0;
  }
  TGFX_PRIVATE_ACCESS(return CountUnpaintedEntries(layer->getContent());)
}

// Size of each quadrant in the shared-corner case below.
static constexpr float CornerQuadrantSize = 32.0f;
// The geometry records hard coverage in the cases a per-painter rule could get wrong: two
// geometries describing one square (bare geometry or a 0%-alpha painter), an image-shaded painter,
// and four geometries meeting at one corner.
TGFX_TEST_PRIVATE(LayerTest, GeometryCoverageContract) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto glass = MakeGeometryGlass(0, 50, 100, 0, 0);

  // Two triangles describing one square must record what the single square records, in both
  // recording channels (see GeometryCoversItsPainters for the 0.75 seam they guard against).
  constexpr float panelSize = 64.0f;
  const auto paths = MakeSplitSquarePaths(panelSize);
  for (auto fillKind : {RegionFillKind::None, RegionFillKind::Transparent}) {
    auto twoGeometries =
        RasterizeGeometryAlpha(context, MakePanel({MakePathRegion(paths.upperRight, fillKind),
                                                   MakePathRegion(paths.lowerLeft, fillKind)},
                                                  {glass}));
    auto oneGeometry = RasterizeGeometryAlpha(
        context, MakePanel({MakePathRegion(paths.square, fillKind)}, {glass}));
    ASSERT_FALSE(twoGeometries.empty());
    ASSERT_EQ(twoGeometries.size(), oneGeometry.size());
    int covered = 0;
    int differing = 0;
    for (size_t i = 0; i < twoGeometries.size(); i++) {
      covered += twoGeometries[i] == 255 ? 1 : 0;
      differing += twoGeometries[i] != oneGeometry[i] ? 1 : 0;
    }
    EXPECT_GT(covered, 0) << "fill=" << static_cast<int>(fillKind)
                          << ": the outline must cover something, otherwise this arm is vacuous";
    EXPECT_EQ(differing, 0) << "fill=" << static_cast<int>(fillKind)
                            << ": two geometries describing one square must match the single one";
    EXPECT_EQ(twoGeometries[32 * 200 + 32], 255)
        << "fill=" << static_cast<int>(fillKind) << ": the shared diagonal stays fully covered";
  }

  // An image-shaded painter: the sampled color alpha must not soften the geometric edge.
  auto image = MakeImage("resources/apitest/checker_128.png");
  ASSERT_TRUE(image != nullptr);
  Path rect = {};
  rect.addRect(Rect::MakeXYWH(0.5f, 0.5f, 63.0f, 63.0f));
  auto imageShape = ShapePath::Make();
  imageShape->setPath(rect);
  auto imageGroup = VectorGroup::Make();
  imageGroup->setElements({imageShape, FillStyle::Make(ImagePattern::Make(image))});
  auto imageLayer = VectorLayer::Make();
  imageLayer->setContents({imageGroup});
  imageLayer->setLayerStyles({glass});
  auto imageAlpha = RasterizeGeometryAlpha(context, imageLayer);
  ASSERT_FALSE(imageAlpha.empty());
  for (auto value : imageAlpha) {
    ASSERT_TRUE(value == 0 || value == 255) << "an image painter must record hard coverage";
  }
  EXPECT_EQ(imageAlpha[32 * 200 + 32], 255) << "the interior is covered";
  EXPECT_EQ(imageAlpha[70 * 200 + 70], 0) << "outside stays uncovered";

  // Four geometries meeting at one corner: each covers a quarter of that pixel.
  for (auto fillKind : {RegionFillKind::None, RegionFillKind::Transparent}) {
    auto makeQuadrant = [fillKind](float x, float y) {
      Path path = {};
      path.addRect(Rect::MakeXYWH(x, y, CornerQuadrantSize, CornerQuadrantSize));
      return MakePathRegion(path, fillKind);
    };
    auto cornerPanel =
        MakePanel({makeQuadrant(0.5f, 0.5f), makeQuadrant(0.5f + CornerQuadrantSize, 0.5f),
                   makeQuadrant(0.5f, 0.5f + CornerQuadrantSize),
                   makeQuadrant(0.5f + CornerQuadrantSize, 0.5f + CornerQuadrantSize)},
                  {glass});
    auto cornerAlpha = RasterizeGeometryAlpha(context, cornerPanel);
    ASSERT_FALSE(cornerAlpha.empty());
    EXPECT_EQ(cornerAlpha[32 * 200 + 32], 255)
        << "fill=" << static_cast<int>(fillKind)
        << ": the pixel shared by four quadrants stays covered";
    EXPECT_EQ(cornerAlpha[24 * 200 + 24], 255)
        << "fill=" << static_cast<int>(fillKind) << ": the quadrant interiors stay covered";
  }
}

// A bare geometry and the same geometry captured by a 0%-alpha painter both end up in the geometry
// recording; the painter additionally reaches the shared contour. The geometry is an opaque,
// hard-edged coverage either way, so both must cover exactly the same pixels, boundary included, on
// the raster and on rendered pixels. A DISJOINT second painter is the
// non-vacuity control, so the equality cannot pass on the painter simply being dropped.
TGFX_TEST_PRIVATE(LayerTest, GeometryCoincidentPaintersDoNotGrowTheCoveredArea) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // A half-pixel offset, so a boundary pixel sits exactly on the shape edge: it is the pixel a
  // shared-coverage over-cover would flip to full.
  const Rect bareRect = Rect::MakeXYWH(60.5f, 60.5f, 80.0f, 80.0f);
  const Rect disjointRect = Rect::MakeXYWH(150.5f, 150.5f, 30.0f, 30.0f);
  const auto disjointArea =
      static_cast<int>(disjointRect.width()) * static_cast<int>(disjointRect.height());

  auto makePanel = [&](bool withUserFill, bool coincidentFill) {
    auto layer = VectorLayer::Make();
    auto bareShape = Rectangle::Make();
    bareShape->setPosition({bareRect.centerX(), bareRect.centerY()});
    bareShape->setSize({bareRect.width(), bareRect.height()});
    // Bare geometry: no painter captures it, so only the geometry channel records it.
    std::vector<std::shared_ptr<VectorElement>> elements = {bareShape};
    if (withUserFill && coincidentFill) {
      // A retained 0%-alpha fill on the SAME geometry element: style alpha 0 over a color alpha of
      // 1, so it paints nothing while its painter captures the geometry. The only difference from
      // the bare panel is which channel records the same geometry -- the exact pair under test, with
      // the geometry count and the panel structure unchanged.
      elements.push_back(MakeZeroAlphaFill());
    }
    auto group = VectorGroup::Make();
    group->setElements(std::move(elements));
    std::vector<std::shared_ptr<VectorElement>> contents = {group};
    if (withUserFill && !coincidentFill) {
      // Non-vacuity control: the same 0%-alpha fill, but on its own DISJOINT geometry, so its
      // painter has to add its own area to the geometry.
      auto fillShape = Rectangle::Make();
      fillShape->setPosition({disjointRect.centerX(), disjointRect.centerY()});
      fillShape->setSize({disjointRect.width(), disjointRect.height()});
      auto fill = FillStyle::Make(SolidColor::Make(Color::White()));
      fill->setAlpha(0.0f);
      auto fillGroup = VectorGroup::Make();
      fillGroup->setElements({fillShape, fill});
      contents.push_back(fillGroup);
    }
    layer->setContents(std::move(contents));
    layer->setLayerStyles({GlassStyle::Make(0, 50, 100, 0, 0, 0, 0)});
    return layer;
  };

  auto countFull = [](const std::vector<int>& raster) {
    int count = 0;
    for (auto value : raster) {
      if (value == 255) {
        count++;
      }
    }
    return count;
  };

  auto bareOnly = RasterizeGeometryAlpha(context, makePanel(false, false));
  auto coincident = RasterizeGeometryAlpha(context, makePanel(true, true));
  auto disjoint = RasterizeGeometryAlpha(context, makePanel(true, false));
  ASSERT_FALSE(bareOnly.empty());
  ASSERT_EQ(bareOnly.size(), coincident.size());
  ASSERT_EQ(bareOnly.size(), disjoint.size());

  const auto bareFull = countFull(bareOnly);
  const auto coincidentFull = countFull(coincident);
  const auto disjointFull = countFull(disjoint);
  EXPECT_GT(bareFull, 0) << "the bare-geometry outline must cover something, otherwise the "
                            "comparisons below are vacuous";
  EXPECT_EQ(coincidentFull, bareFull)
      << "coincident-fill outline: a second painter describing the SAME geometry must not add any "
         "fully covered pixel";
  int differing = 0;
  for (size_t i = 0; i < bareOnly.size(); i++) {
    if (bareOnly[i] != coincident[i]) {
      differing++;
    }
  }
  EXPECT_EQ(differing, 0)
      << "coincident-fill outline: the whole raster must match the bare-geometry outline pixel for "
         "pixel, boundary included; "
      << differing << " pixels differ";
  // Non-vacuity: the same fixture with the second painter DISJOINT does add its own area, so the
  // equality above cannot pass on the fill being dropped from the geometry.
  EXPECT_EQ(disjointFull - bareFull, disjointArea)
      << "disjoint-fill outline: the second painter must be recorded, so it has to add exactly its "
         "own "
      << disjointArea << "-pixel area";

  // The same boundary on rendered pixels: both configurations must frost the checker backdrop
  // identically, or the extra painter would be visible as a fringe.
  auto renderedBareOnly = RenderPanelOverChecker(context, makePanel(false, false));
  auto renderedCoincident = RenderPanelOverChecker(context, makePanel(true, true));
  auto renderedDisjoint = RenderPanelOverChecker(context, makePanel(true, false));
  ASSERT_TRUE(renderedBareOnly != nullptr && renderedCoincident != nullptr &&
              renderedDisjoint != nullptr);
  const auto wholeSurface = Rect::MakeLTRB(0.0f, 0.0f, 200.0f, 200.0f);
  EXPECT_EQ(CountDifferingPixels(renderedBareOnly.get(), renderedCoincident.get(), wholeSurface, 2),
            0)
      << "rendered checker ROI: the coincident 0%-alpha fill must leave the Glass render untouched";
  EXPECT_GT(CountDifferingPixels(renderedBareOnly.get(), renderedDisjoint.get(), wholeSurface, 2),
            0)
      << "rendered checker ROI (disjoint control): the render comparison must be able to see an "
         "extra fill at all, or the equality above is vacuous";
}

// A bare geometry only reaches the layer bounds while some style declares the geometry as its
// content. Without such a style the layer paints nothing and must stay invisible, so its bounds
// (and with them the offscreen capture and the dirty region of everything that uses them) stay
// empty. The gate has to re-arm on both edges, which means removing the style has to drop the
// cached content bounds as well - hence the same layer is measured with and without the style.
TGFX_TEST(LayerTest, GeometryBoundsFollowStyleContentSource) {
  const Rect bareRect = Rect::MakeXYWH(20.0f, 30.0f, 40.0f, 25.0f);
  auto makeGeometry = [&bareRect]() {
    auto rect = Rectangle::Make();
    rect->setPosition({bareRect.centerX(), bareRect.centerY()});
    rect->setSize({bareRect.width(), bareRect.height()});
    return rect;
  };
  auto makeLayer = [&](bool withOpaqueFill) {
    std::vector<std::shared_ptr<VectorElement>> elements = {makeGeometry()};
    if (withOpaqueFill) {
      elements.push_back(FillStyle::Make(SolidColor::Make(Color::White())));
    }
    auto group = VectorGroup::Make();
    group->setElements(std::move(elements));
    auto layer = VectorLayer::Make();
    layer->setContents({group});
    return layer;
  };

  // Control: a visible fill gives the same geometry a non-empty content bounds, so an empty bounds
  // below cannot be the fixture having no geometry at all.
  auto filledLayer = makeLayer(true);
  EXPECT_FALSE(filledLayer->getBounds().isEmpty())
      << "control fixture: an opaque fill must produce content bounds";
  EXPECT_FALSE(filledLayer->getBounds(nullptr, true).isEmpty()) << "control fixture: tight bounds";

  auto layer = makeLayer(false);
  EXPECT_TRUE(layer->getBounds().isEmpty())
      << "a bare geometry without a Geometry-sourced style must not give the layer any bounds";
  EXPECT_TRUE(layer->getBounds(nullptr, true).isEmpty()) << "tight bounds, no style";

  layer->setLayerStyles({MakeGeometryGlass(0, 50, 100, 0, 0)});
  EXPECT_EQ(layer->getBounds(), bareRect)
      << "the Glass style consumes the geometry, so the bounds must cover exactly it";
  EXPECT_EQ(layer->getBounds(nullptr, true), bareRect)
      << "tight bounds must cover the bare geometry as well";

  layer->setLayerStyles({});
  EXPECT_TRUE(layer->getBounds().isEmpty())
      << "removing the style must drop the geometry from the bounds again";
  EXPECT_TRUE(layer->getBounds(nullptr, true).isEmpty()) << "tight bounds must be dropped again";
}

// ===== The geometry: which styles see it, and how it is composited =====

// A rectangle element covering `bounds`, for the cases below that need geometry and painter in one
// flat element list (a Repeater or a MergePath only applies to the elements of its own group).
static std::shared_ptr<Rectangle> MakeRectElement(const Rect& bounds) {
  auto rect = Rectangle::Make();
  rect->setPosition({bounds.centerX(), bounds.centerY()});
  rect->setSize({bounds.width(), bounds.height()});
  return rect;
}

// Alpha of an geometry raster at (x, y), in layer-local coordinates.
static int GeometryAlphaAt(const std::vector<int>& raster, int x, int y) {
  if (x < 0 || y < 0 || x >= GeometryProbeSize || y >= GeometryProbeSize ||
      raster.size() != static_cast<size_t>(GeometryProbeSize) * GeometryProbeSize) {
    return -1;
  }
  return raster[static_cast<size_t>(y) * GeometryProbeSize + static_cast<size_t>(x)];
}

// Records the LayerStyleInput a layer hands to one style, so both the kind of recording and the
// shape travelling with it can be asserted without a style that consumes them.
class GeometryShapeProbeStyle : public LayerStyle {
 public:
  explicit GeometryShapeProbeStyle(LayerStyleContentSource source) : _contentSource(source) {
  }

  LayerStylePosition position() const override {
    return LayerStylePosition::Above;
  }

  Rect filterBounds(const Rect& srcRect, float /*contentScale*/) override {
    return srcRect;
  }

  LayerStyleContentSource contentSource() const override {
    return _contentSource;
  }

  // Filled in by onDraw, so a passing assertion also proves the style was drawn at all. content is
  // held by reference count on purpose: it keeps the recording object alive for the identity
  // assertions of the shared-source cases.
  bool drawn = false;
  bool sawGeometryShape = false;
  bool sawContentImage = false;
  StyleInputSource::Type contentType = StyleInputSource::Type::Base;
  std::shared_ptr<StyleInputSource> content = nullptr;

 protected:
  void onDraw(Canvas* /*canvas*/, const LayerStyleInput& input, float /*alpha*/,
              BlendMode /*blendMode*/) override {
    drawn = true;
    content = input.content;
    if (input.content == nullptr) {
      return;
    }
    contentType = input.content->type();
    sawContentImage = input.content->image() != nullptr;
    if (input.content->type() == StyleInputSource::Type::Geometry) {
      const auto* geometrySource = static_cast<const GeometryInputSource*>(input.content.get());
      sawGeometryShape = geometrySource->shape().has_value();
    }
  }

 private:
  LayerStyleContentSource _contentSource = LayerStyleContentSource::All;
};

// Only the geometry recording carries an exact shape, and only a style that declares that
// content source receives it. A sibling in the same excludeChildEffects bucket shares the recorded
// group, and its content is the shared recording, which that shape does not describe: pairing the
// two would make the sibling read a footprint it never drew.
TGFX_TEST_PRIVATE(LayerTest, GeometryShapeIsOnlyGivenToGeometrySourceStyles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto otherStyle = std::make_shared<GeometryShapeProbeStyle>(LayerStyleContentSource::All);
  auto geometryStyle = std::make_shared<GeometryShapeProbeStyle>(LayerStyleContentSource::Geometry);
  // The parent holds one bare geometry and paints nothing itself, so it records exactly one
  // geometry outline. The painted child keeps the shared content non-empty, so a style drawing from
  // the content is drawn instead of skipped -- otherwise the assertion below would be vacuous.
  auto parent = VectorLayer::Make();
  RegionSpec bareGeometry = {};
  bareGeometry.bounds = GlassPanelLocalBounds();
  parent->setContents({MakeRegion(bareGeometry)});
  auto child = VectorLayer::Make();
  RegionSpec paintedRegion = {};
  paintedRegion.bounds = GlassPanelLocalBounds();
  paintedRegion.fill = RegionFillKind::Opaque;
  child->setContents({MakeRegion(paintedRegion)});
  parent->addChild(child);
  parent->setLayerStyles({otherStyle, geometryStyle});

  auto surface = RenderPanelOverChecker(context, parent);
  ASSERT_TRUE(surface != nullptr);

  TGFX_PRIVATE_ACCESS(
      DrawArgs drawArgs(context); auto source = parent->getLayerStyleSource(drawArgs, Matrix::I());
      ASSERT_TRUE(source != nullptr); ASSERT_TRUE(source->groups[0] != nullptr);
      ASSERT_TRUE(source->groups[0]->geometry != nullptr)
      << "control: the group has to carry the geometry, or the gating below proves nothing";
      ASSERT_TRUE(source->groups[0]->geometry->shape().has_value())
      << "control: the group has to carry the geometry shape, or the gating below proves nothing";)

  EXPECT_TRUE(geometryStyle->drawn);
  EXPECT_TRUE(otherStyle->drawn) << "control: the shared content must be drawable";
  EXPECT_TRUE(geometryStyle->sawGeometryShape)
      << "a style declaring LayerStyleContentSource::Geometry must receive the geometry shape";
  EXPECT_FALSE(otherStyle->sawGeometryShape)
      << "a style drawing from the shared content must not receive a geometry shape that does not "
         "describe that content";
}

// ===== The typed content source: which recording a style is handed =====

// A layer painting its geometry hands a Geometry-sourced style that recording, together with the
// exact outline read back from it.
TGFX_TEST_PRIVATE(LayerTest, StyleInputContentIsGeometrySource) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto probe = std::make_shared<GeometryShapeProbeStyle>(LayerStyleContentSource::Geometry);
  probe->setExcludeChildEffects(true);
  RegionSpec painted = {};
  painted.bounds = GlassPanelLocalBounds();
  painted.fill = RegionFillKind::Opaque;
  ASSERT_TRUE(RenderPanelOverChecker(context, MakePanel({painted}, {probe})) != nullptr);

  ASSERT_TRUE(probe->drawn) << "control: the style has to be drawn, or nothing was asserted";
  ASSERT_TRUE(probe->content != nullptr) << "a painted layer records geometry";
  EXPECT_EQ(probe->contentType, StyleInputSource::Type::Geometry)
      << "the recording a Geometry-sourced style samples is its geometry";
  EXPECT_TRUE(probe->sawContentImage);
  EXPECT_TRUE(probe->sawGeometryShape)
      << "one rectangle has an exact outline, so it travels with the recording";
}

// A layer whose geometry lives in its children records no geometry, so a Geometry-sourced style
// falls back to the shared contour. That contour is this bucket's own raster and no exact outline
// describes it, so the shape must stay empty.
TGFX_TEST_PRIVATE(LayerTest, StyleInputFallbackReportsContourWithoutShape) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto probe = std::make_shared<GeometryShapeProbeStyle>(LayerStyleContentSource::Geometry);
  probe->setExcludeChildEffects(true);
  auto parent = Layer::Make();
  parent->setMatrix(Matrix::MakeTrans(GlassPanelLeft, GlassPanelTop));
  auto child = VectorLayer::Make();
  RegionSpec painted = {};
  painted.bounds = GlassPanelLocalBounds();
  painted.fill = RegionFillKind::Opaque;
  child->setContents({MakeRegion(painted)});
  parent->addChild(child);
  parent->setLayerStyles({probe});
  ASSERT_TRUE(RenderPanelOverChecker(context, parent) != nullptr);

  ASSERT_TRUE(probe->drawn) << "control: the fallback still draws the style";
  ASSERT_TRUE(probe->content != nullptr);
  EXPECT_EQ(probe->contentType, StyleInputSource::Type::Contour)
      << "a layer painting nothing owns no geometry, so the style is handed the shared contour";
  EXPECT_TRUE(probe->sawContentImage);
  EXPECT_FALSE(probe->sawGeometryShape)
      << "the fallback contour is no exact outline, so it must not report a shape";
}

// A style that does not declare Geometry samples the shared content, whose kind is Base and which
// carries no shape either.
TGFX_TEST_PRIVATE(LayerTest, StyleInputContentIsBaseForNonGeometryStyle) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto probe = std::make_shared<GeometryShapeProbeStyle>(LayerStyleContentSource::All);
  probe->setExcludeChildEffects(true);
  RegionSpec painted = {};
  painted.bounds = GlassPanelLocalBounds();
  painted.fill = RegionFillKind::Opaque;
  ASSERT_TRUE(RenderPanelOverChecker(context, MakePanel({painted}, {probe})) != nullptr);

  ASSERT_TRUE(probe->drawn);
  ASSERT_TRUE(probe->content != nullptr) << "control: the shared content is drawable";
  EXPECT_EQ(probe->contentType, StyleInputSource::Type::Base);
  EXPECT_TRUE(probe->sawContentImage);
  EXPECT_FALSE(probe->sawGeometryShape) << "the shared content has no single exact outline";
}

// The geometry is recorded once per layer, so both excludeChildEffects buckets must hand out the
// very same source object: one recording, one typed wrapper, no per-bucket copy.
TGFX_TEST_PRIVATE(LayerTest, StyleInputGeometrySourceSharedAcrossStyles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto seesChildren = std::make_shared<GeometryShapeProbeStyle>(LayerStyleContentSource::Geometry);
  seesChildren->setExcludeChildEffects(false);
  auto seesOwnOnly = std::make_shared<GeometryShapeProbeStyle>(LayerStyleContentSource::Geometry);
  seesOwnOnly->setExcludeChildEffects(true);
  RegionSpec painted = {};
  painted.bounds = GlassPanelLocalBounds();
  painted.fill = RegionFillKind::Opaque;
  ASSERT_TRUE(RenderPanelOverChecker(context, MakePanel({painted}, {seesChildren, seesOwnOnly})) !=
              nullptr);

  ASSERT_TRUE(seesChildren->drawn && seesOwnOnly->drawn);
  ASSERT_TRUE(seesChildren->content != nullptr && seesOwnOnly->content != nullptr);
  EXPECT_EQ(seesChildren->contentType, StyleInputSource::Type::Geometry);
  EXPECT_EQ(seesChildren->content, seesOwnOnly->content)
      << "both buckets must share one geometry source instead of wrapping the recording twice";
}

// Records which image a background-sourced style is handed. A background source makes the layer go
// through the capture/consume chain, which is the only place the recorded pictures are replaced by
// their rasterized wrappers.
class RasterizedContentProbeStyle : public LayerStyle {
 public:
  // Filled in by onDraw, so a passing assertion also proves the style was drawn at all.
  bool drawn = false;
  bool contentIsRasterized = false;
  // Held by reference count on purpose: it keeps the wrapper alive for the identity assertion.
  std::shared_ptr<Image> contentImage = nullptr;
  StyleInputSource::Type contentType = StyleInputSource::Type::Base;

  LayerStylePosition position() const override {
    return LayerStylePosition::Below;
  }

  Rect filterBounds(const Rect& srcRect, float /*contentScale*/) override {
    return srcRect;
  }

  uint32_t extraSourceType() const override {
    return static_cast<uint32_t>(LayerStyleExtraSourceType::Background);
  }

  LayerStyleContentSource contentSource() const override {
    return LayerStyleContentSource::Geometry;
  }

 protected:
  // A background-sourced style must ask for something outside its content, otherwise the layer tree
  // skips the whole capture pass on the steady-state path (hasBackgroundStyle() only reads the
  // accumulated outset there) and the style would never be dispatched. One pixel of full-resolution
  // backdrop is enough for a probe that draws nothing.
  Rect filterBackgroundSharp(const Rect& srcRect, float /*contentScale*/) override {
    auto bounds = srcRect;
    bounds.outset(1.0f, 1.0f);
    return bounds;
  }

  void onDraw(Canvas* /*canvas*/, const LayerStyleInput& input, float /*alpha*/,
              BlendMode /*blendMode*/) override {
    drawn = true;
    if (input.content == nullptr) {
      return;
    }
    contentType = input.content->type();
    contentImage = input.content->image();
    contentIsRasterized = std::dynamic_pointer_cast<RasterizedImage>(contentImage) != nullptr;
  }
};

// The background chain replaces the recorded pictures with rasterized wrappers so the capture pass
// uploads a texture the consume pass reuses, and the typed sources built from those recordings must
// follow them. A style sampling the wrapper the recording used to hold would replay the picture and
// lose the sharing, so the source has to be rebuilt after the swap: this is the only path that swaps
// them, and none of the drawLayerStyleDefault cases can see it.
TGFX_TEST_PRIVATE(LayerTest, StyleInputFollowsRasterizedGeometry) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // One style per excludeChildEffects bucket: the geometry is recorded once and both entries
  // hold that one recording, so the swap has to wrap it once and hand the same wrapper out.
  auto seesChildren = std::make_shared<RasterizedContentProbeStyle>();
  seesChildren->setExcludeChildEffects(false);
  auto seesOwnOnly = std::make_shared<RasterizedContentProbeStyle>();
  seesOwnOnly->setExcludeChildEffects(true);
  RegionSpec painted = {};
  painted.bounds = GlassPanelLocalBounds();
  painted.fill = RegionFillKind::Opaque;
  ASSERT_TRUE(RenderPanelOverChecker(context, MakePanel({painted}, {seesChildren, seesOwnOnly})) !=
              nullptr);

  ASSERT_TRUE(seesChildren->drawn && seesOwnOnly->drawn)
      << "control: the background chain has to draw the style";
  EXPECT_EQ(seesChildren->contentType, StyleInputSource::Type::Geometry);
  EXPECT_EQ(seesOwnOnly->contentType, StyleInputSource::Type::Geometry);
  EXPECT_TRUE(seesChildren->contentIsRasterized && seesOwnOnly->contentIsRasterized)
      << "both styles must sample the rasterized geometry, not the picture behind it";
  EXPECT_TRUE(seesChildren->contentImage == seesOwnOnly->contentImage)
      << "one recording, one wrapper: both buckets must sample the same image object";
}

// The swap that wraps the recordings for the capture pass lives in BackgroundHandler, not in any
// Layer::drawLayerStyleDefault path, so its two invariants need their own case: after the swap every
// bucket's source must hold the rasterized image, and the recording the buckets share must be
// wrapped once so both consumers sample one texture.
TGFX_TEST_PRIVATE(LayerTest, LayerStyleSourceSwapSharesOneRasterizedGeometry) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto seesChildren = std::make_shared<GeometryShapeProbeStyle>(LayerStyleContentSource::Geometry);
  seesChildren->setExcludeChildEffects(false);
  auto seesOwnOnly = std::make_shared<GeometryShapeProbeStyle>(LayerStyleContentSource::Geometry);
  seesOwnOnly->setExcludeChildEffects(true);
  RegionSpec painted = {};
  painted.bounds = GlassPanelLocalBounds();
  painted.fill = RegionFillKind::Opaque;
  auto panel = MakePanel({painted}, {seesChildren, seesOwnOnly});

  TGFX_PRIVATE_ACCESS(
      DrawArgs drawArgs(context); auto source = panel->getLayerStyleSource(drawArgs, Matrix::I());
      ASSERT_TRUE(source != nullptr); ASSERT_TRUE(source->groups[0] != nullptr);
      ASSERT_TRUE(source->groups[1] != nullptr);
      ASSERT_TRUE(source->groups[0]->geometry != nullptr);
      ASSERT_TRUE(source->groups[1]->geometry != nullptr);
      auto recording = source->groups[0]->geometry->image(); ASSERT_TRUE(recording != nullptr);
      EXPECT_TRUE(recording == source->groups[1]->geometry->image())
      << "control: both buckets start from the one geometry recording";
      BackgroundSnapshotMap snapshots = {}; BackgroundCapturer capturer(&snapshots, nullptr);
      auto* cached = capturer.cacheLayerStyleSource(panel.get(), std::move(source));
      ASSERT_TRUE(cached != nullptr); ASSERT_TRUE(cached->groups[0] != nullptr);
      ASSERT_TRUE(cached->groups[1] != nullptr);
      ASSERT_TRUE(cached->groups[0]->geometry != nullptr);
      ASSERT_TRUE(cached->groups[1]->geometry != nullptr);
      EXPECT_TRUE(cached->groups[0]->geometry->image() == cached->groups[1]->geometry->image())
      << "wrapping the shared recording per bucket would give one picture two "
         "wrappers";
      EXPECT_TRUE(cached->groups[0]->geometry->image() != recording)
      << "the cached source must wrap the rasterized image, not the picture";
      EXPECT_TRUE(cached->groups[1]->geometry->shape().has_value())
      << "the rebuilt sources must keep carrying the shape read back before the swap";)
}

// A bare geometry next to a painter has no single exact outline, so the style is handed no geometry shape
// and falls back to the alpha-mask path. The raster keeps covering both regions either way.
TGFX_TEST_PRIVATE(LayerTest, GeometryShapeIsDroppedWhenPaintersCoexist) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto glass = MakeGeometryGlass(0, 50, 100, 0, 0);
  RegionSpec painted = {};
  painted.bounds = Rect::MakeXYWH(20.0f, 20.0f, 40.0f, 30.0f);
  painted.fill = RegionFillKind::Opaque;
  RegionSpec bareGeometry = {};
  bareGeometry.bounds = Rect::MakeXYWH(100.0f, 100.0f, 40.0f, 30.0f);

  auto both = MakePanel({painted, bareGeometry}, {glass});
  TGFX_PRIVATE_ACCESS(
      DrawArgs drawArgs(context); auto source = both->getLayerStyleSource(drawArgs, Matrix::I());
      ASSERT_TRUE(source != nullptr); ASSERT_TRUE(source->groups[1] != nullptr);
      ASSERT_TRUE(source->groups[1]->geometry != nullptr);
      EXPECT_FALSE(source->groups[1]->geometry->shape().has_value())
      << "a painter and a bare geometry together have no single exact outline, so the style must "
         "take the alpha-mask path instead of an SDF that would cover less than the raster";)

  // Dropping the shape drops no coverage: the painter contour and the bare geometry are both in the
  // raster the alpha mask reads.
  auto alpha = RasterizeGeometryAlpha(context, both);
  ASSERT_FALSE(alpha.empty());
  EXPECT_EQ(GeometryAlphaAt(alpha, 40, 35), 255) << "the painted region is covered";
  EXPECT_EQ(GeometryAlphaAt(alpha, 120, 115), 255) << "the bare region is covered";
  EXPECT_EQ(GeometryAlphaAt(alpha, 80, 80), 0) << "the gap between them is not";

  // Control: the same bare geometry alone does have an exact outline, so the nullopt above is the
  // painter being present rather than the fixture having no shape at all.
  auto bareOnly = MakePanel({bareGeometry}, {glass});
  TGFX_PRIVATE_ACCESS(DrawArgs bareArgs(context);
                      auto bareSource = bareOnly->getLayerStyleSource(bareArgs, Matrix::I());
                      ASSERT_TRUE(bareSource != nullptr && bareSource->groups[1] != nullptr);
                      ASSERT_TRUE(bareSource->groups[1]->geometry != nullptr);
                      EXPECT_TRUE(bareSource->groups[1]->geometry->shape().has_value());)
}

// ===== The geometry shape comes out of the geometry recording =====
//
// The shape a Geometry-sourced style consumes is no longer derived from the elements or painters;
// it is read back from the very picture the geometry raster is made of. The cases below pin what
// that playback accepts: one outline drawn any number of times collapses to a single shape, and
// everything the recording cannot express as that one outline rejects the shape while leaving the
// raster untouched.

// What a Geometry-sourced bucket recorded for `layer`: whether it holds an geometry raster and
// the shape read back from that same recording. matrix is the one getLayerStyleSource() is called
// with, and its max scale is what the recording is scaled by.
struct GeometryProbe {
  bool hasImage = false;
  std::optional<StyledShape> shape = std::nullopt;
};

static GeometryProbe ProbeGeometry(Context* context, const std::shared_ptr<Layer>& layer,
                                   const Matrix& matrix = Matrix::I()) {
  GeometryProbe probe = {};
  TGFX_PRIVATE_ACCESS(
      DrawArgs drawArgs(context); auto source = layer->getLayerStyleSource(drawArgs, matrix);
      if (source == nullptr) { return probe; } for (auto& group
                                                    : source->groups) {
        if (group != nullptr && group->geometry != nullptr) {
          probe.hasImage = group->geometry->image() != nullptr;
          probe.shape = group->geometry->shape();
        }
      })
  return probe;
}

// One rectangle outline painted twice: the retained 0%-alpha fill keeps the geometry in the
// geometry recording next to the visible fill, and both paints draw the very same rectangle. Two
// draws that agree describe one shape, so the style keeps the SDF path it had before.
TGFX_TEST_PRIVATE(LayerTest, GeometryShapeCollapsesDuplicateGeometry) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto rect = Rectangle::Make();
  rect->setPosition({50.0f, 40.0f});
  rect->setSize({100.0f, 80.0f});
  auto group = VectorGroup::Make();
  group->setElements(
      {rect, MakeZeroAlphaFill(), FillStyle::Make(SolidColor::Make(Color::White()))});
  auto layer = VectorLayer::Make();
  layer->setContents({group});
  layer->setLayerStyles({MakeGeometryGlass(0, 50, 100, 0, 0)});

  auto probe = ProbeGeometry(context, layer);
  ASSERT_TRUE(probe.hasImage) << "control: the geometry is recorded";
  ASSERT_TRUE(probe.shape.has_value())
      << "two paints over one outline are still a single outline, so the shape must survive";
  ASSERT_TRUE(probe.shape->shape != nullptr);
  EXPECT_EQ(probe.shape->shape->getBounds(), Rect::MakeWH(100.0f, 80.0f))
      << "the shape has to be in layer coordinates, or the SDF would be offset from its pixels";
}

// One geometry two fill painters captured is drawn once per painter, so the recording hands over
// the very same Shape instance twice. This covers that same-instance fast path: the second draw
// reuses the outline already held instead of building the path again.
TGFX_TEST_PRIVATE(LayerTest, GeometryShapeCollapsesRepeatedShapeInstance) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Path path = {};
  path.addRect(Rect::MakeWH(100.0f, 80.0f));
  auto shapePath = ShapePath::Make();
  shapePath->setPath(std::move(path));
  // Both fills capture the one geometry element, so both draws carry its single Shape instance.
  auto first = FillStyle::Make(SolidColor::Make(Color::White()));
  auto second = FillStyle::Make(SolidColor::Make(Color::Red()));
  auto group = VectorGroup::Make();
  group->setElements({shapePath, first, second});
  auto layer = VectorLayer::Make();
  layer->setContents({group});
  layer->setLayerStyles({MakeGeometryGlass(0, 50, 100, 0, 0)});

  auto probe = ProbeGeometry(context, layer);
  ASSERT_TRUE(probe.hasImage) << "control: the geometry is recorded";
  ASSERT_TRUE(probe.shape.has_value())
      << "the same shape instance drawn twice is one outline, so the shape must survive";
  ASSERT_TRUE(probe.shape->shape != nullptr);
  EXPECT_EQ(probe.shape->shape->getBounds(), Rect::MakeWH(100.0f, 80.0f));
}

// Two different outlines cannot be one shape, so the style must fall back to the alpha mask -- while
// the raster it reads keeps covering both regions.
TGFX_TEST_PRIVATE(LayerTest, GeometryShapeRejectsDistinctGeometry) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  RegionSpec left = {};
  left.bounds = Rect::MakeXYWH(10.0f, 10.0f, 40.0f, 30.0f);
  left.fill = RegionFillKind::Opaque;
  RegionSpec right = {};
  right.bounds = Rect::MakeXYWH(70.0f, 40.0f, 40.0f, 30.0f);
  right.fill = RegionFillKind::Opaque;

  auto panel = MakePanel({left, right}, {MakeGeometryGlass(0, 50, 100, 0, 0)});
  auto probe = ProbeGeometry(context, panel);
  EXPECT_FALSE(probe.shape.has_value())
      << "two disjoint outlines have no single-shape expression, so a shape here would cover "
         "neither of them exactly";
  EXPECT_TRUE(probe.hasImage) << "dropping the shape must not drop the raster the alpha mask reads";

  // Coverage of the same panel: the fallback keeps drawing over both regions, so the nullopt above
  // is the shape being dropped and not the recording being empty.
  auto alpha = RasterizeGeometryAlpha(context, panel);
  ASSERT_FALSE(alpha.empty());
  EXPECT_EQ(GeometryAlphaAt(alpha, 30, 25), 255) << "the left region is covered";
  EXPECT_EQ(GeometryAlphaAt(alpha, 90, 55), 255) << "the right region is covered";

  // Control: either region alone does have a shape, so the rejection above is the second outline.
  auto singleProbe =
      ProbeGeometry(context, MakePanel({left}, {MakeGeometryGlass(0, 50, 100, 0, 0)}));
  EXPECT_TRUE(singleProbe.shape.has_value());
}

// Two outlines covering the very same box still differ, and treating them as equal would hand the
// style a shape that covers pixels the layer never painted. This case exists to keep the comparison
// on the path itself: a bounds comparison passes it.
TGFX_TEST_PRIVATE(LayerTest, GeometryShapeRejectsSameBoundsDifferentPaths) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const Rect sharedBounds = Rect::MakeXYWH(10.0f, 10.0f, 100.0f, 80.0f);
  RegionSpec rect = {};
  rect.bounds = sharedBounds;
  rect.fill = RegionFillKind::Opaque;
  RegionSpec oval = {};
  oval.bounds = sharedBounds;
  oval.fill = RegionFillKind::Opaque;
  // The ellipse takes its box from its position and size, like the rectangle does.
  auto ellipse = Ellipse::Make();
  ellipse->setPosition({sharedBounds.centerX(), sharedBounds.centerY()});
  ellipse->setSize({sharedBounds.width(), sharedBounds.height()});
  oval.shape = ellipse;

  // Premise: the two fixtures really do share a bounding box, and each alone has a shape. Without
  // this, a nullopt below could come from the ellipse failing to resolve a path at all.
  auto rectProbe = ProbeGeometry(context, MakePanel({rect}, {MakeGeometryGlass(0, 50, 100, 0, 0)}));
  auto ovalProbe = ProbeGeometry(context, MakePanel({oval}, {MakeGeometryGlass(0, 50, 100, 0, 0)}));
  ASSERT_TRUE(rectProbe.shape.has_value());
  ASSERT_TRUE(ovalProbe.shape.has_value());
  ASSERT_TRUE(rectProbe.shape->shape != nullptr && ovalProbe.shape->shape != nullptr);
  ASSERT_EQ(rectProbe.shape->shape->getBounds(), ovalProbe.shape->shape->getBounds())
      << "the fixtures must share a bounding box, or this case is not about same-bounds shapes";
  EXPECT_NE(rectProbe.shape->shape->getPath(), ovalProbe.shape->shape->getPath())
      << "the fixtures must differ as paths, or rejecting them would be wrong";

  auto both = MakePanel({oval, rect}, {MakeGeometryGlass(0, 50, 100, 0, 0)});
  // The ellipse comes first so that both outlines reach the recording: the recording drops a draw
  // whose coverage an earlier one already contains, and the rectangle covers the ellipse entirely.
  auto probe = ProbeGeometry(context, both);
  EXPECT_FALSE(probe.shape.has_value())
      << "a rectangle and its inscribed ellipse share a bounding box and nothing else, so no "
         "single shape describes both";
  EXPECT_TRUE(probe.hasImage);
}

// A brush that samples something other than a flat color does not cover the fill path, so the
// recording cannot be reduced to the path. This is the unsupported-painter case: an image pattern.
TGFX_TEST_PRIVATE(LayerTest, GeometryShapeRejectsUnsupportedPainter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto image = MakeImage("resources/apitest/checker_128.png");
  ASSERT_TRUE(image != nullptr);
  auto makeLayer = [&](const std::shared_ptr<FillStyle>& fill) {
    Path path = {};
    path.addRect(Rect::MakeWH(100.0f, 80.0f));
    auto shape = ShapePath::Make();
    shape->setPath(std::move(path));
    auto group = VectorGroup::Make();
    group->setElements({shape, fill});
    auto layer = VectorLayer::Make();
    layer->setContents({group});
    layer->setLayerStyles({MakeGeometryGlass(0, 50, 100, 0, 0)});
    return layer;
  };

  auto imageProbe = ProbeGeometry(context, makeLayer(FillStyle::Make(ImagePattern::Make(image))));
  EXPECT_FALSE(imageProbe.shape.has_value())
      << "an image-sampled brush covers what the texture holds, not what the path encloses";
  EXPECT_TRUE(imageProbe.hasImage) << "the raster still covers the geometry it recorded";

  // Control: the same geometry with a flat color does produce a shape, so the rejection above comes
  // from the brush and not from the fixture.
  auto flatProbe =
      ProbeGeometry(context, makeLayer(FillStyle::Make(SolidColor::Make(Color::White()))));
  ASSERT_TRUE(flatProbe.shape.has_value());
}

// A gradient brush is not an image brush: the contour recording carries image shaders only, so a
// gradient painter reaches it as a plain opaque brush. Its coverage is still exactly the fill path,
// so the geometry stays extractable and the style keeps the SDF path.
TGFX_TEST_PRIVATE(LayerTest, GeometryShapeKeepsGradientPainter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const Rect bounds = Rect::MakeWH(100.0f, 80.0f);
  Path path = {};
  path.addRect(bounds);
  auto shape = ShapePath::Make();
  shape->setPath(std::move(path));
  auto gradient = Gradient::MakeLinear({0.0f, 0.0f}, {1.0f, 0.0f}, {Color::Red(), Color::Blue()});
  ASSERT_TRUE(gradient != nullptr);
  auto group = VectorGroup::Make();
  group->setElements({shape, FillStyle::Make(gradient)});
  auto layer = VectorLayer::Make();
  layer->setContents({group});
  layer->setLayerStyles({MakeGeometryGlass(0, 50, 100, 0, 0)});

  auto probe = ProbeGeometry(context, layer);
  EXPECT_TRUE(probe.hasImage) << "the gradient fill is still recorded as the geometry raster";
  ASSERT_TRUE(probe.shape.has_value())
      << "contour recording drops non-image shaders, so a gradient painter paints a plain opaque "
         "brush and its geometry stays extractable";
  ASSERT_TRUE(probe.shape->shape != nullptr);
  EXPECT_EQ(probe.shape->shape->getBounds(), bounds)
      << "the shape has to describe the geometry the gradient fills";
}

// An inside/outside stroke is baked into "expanded fill + clip", so the recording holds a filled
// shape that is only correct inside the clip. Keeping it would cover the whole doubled stroke width,
// and this case isolates that clip: the panel carries no fill, so nothing else rejects the shape.
TGFX_TEST_PRIVATE(LayerTest, GeometryShapeRejectsAlignedStrokeClip) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  RegionSpec stroked = {};
  stroked.bounds = Rect::MakeXYWH(20.0f, 20.0f, 100.0f, 80.0f);
  stroked.strokeWidth = 8.0f;
  stroked.strokeAlign = StrokeAlign::Outside;
  auto panel = MakePanel({stroked}, {MakeGeometryGlass(0, 50, 100, 0, 0)});

  auto probe = ProbeGeometry(context, panel);
  EXPECT_FALSE(probe.shape.has_value())
      << "the clipped expanded fill describes twice the visible stroke band, so it must not be "
         "handed over as the layer's shape";
  EXPECT_TRUE(probe.hasImage) << "the band is still recorded, so the alpha mask can draw it";

  // Control: the same rectangle with a centered stroke records a stroke instead of a clip and must
  // be rejected as well; a fill-only rectangle is the accepted counterpart.
  RegionSpec centered = stroked;
  centered.strokeAlign = StrokeAlign::Center;
  EXPECT_FALSE(ProbeGeometry(context, MakePanel({centered}, {MakeGeometryGlass(0, 50, 100, 0, 0)}))
                   .shape.has_value());
  RegionSpec filled = {};
  filled.bounds = stroked.bounds;
  filled.fill = RegionFillKind::Opaque;
  EXPECT_TRUE(ProbeGeometry(context, MakePanel({filled}, {MakeGeometryGlass(0, 50, 100, 0, 0)}))
                  .shape.has_value());
}

// An inverse fill paints the complement of its outline, so the recording covers everything the
// path leaves out. Read back as a shape it would describe the opposite coverage, so it must be
// rejected while the raster keeps the complement. The outline is a triangle because the recorder
// folds a rect/oval/rrect path into a plain rect or rrect record, which drops the fill type before
// the extraction ever sees it.
TGFX_TEST_PRIVATE(LayerTest, GeometryShapeRejectsInverseFillPath) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto makeLayer = [&](PathFillType fillType) {
    Path path = {};
    path.moveTo(0.0f, 0.0f);
    path.lineTo(100.0f, 0.0f);
    path.lineTo(0.0f, 80.0f);
    path.close();
    path.setFillType(fillType);
    auto shapePath = ShapePath::Make();
    shapePath->setPath(std::move(path));
    auto group = VectorGroup::Make();
    group->setElements({shapePath, FillStyle::Make(SolidColor::Make(Color::White()))});
    auto layer = VectorLayer::Make();
    layer->setContents({group});
    layer->setLayerStyles({MakeGeometryGlass(0, 50, 100, 0, 0)});
    return layer;
  };

  auto inverseProbe = ProbeGeometry(context, makeLayer(PathFillType::InverseWinding));
  EXPECT_FALSE(inverseProbe.shape.has_value())
      << "inverse coverage is the complement of the outline, so handing it over as the layer's "
         "shape would invert the area the style covers";
  EXPECT_TRUE(inverseProbe.hasImage)
      << "the complement is still recorded, so the alpha mask can draw the pixels the layer paints";

  // Control: the very same outline filled the normal way does resolve a shape, so the nullopt above
  // is the inverse fill type and not the fixture.
  auto directProbe = ProbeGeometry(context, makeLayer(PathFillType::Winding));
  ASSERT_TRUE(directProbe.shape.has_value());
}

// The recording canvas is scaled by the content scale, so the extracted shape has to undo that
// scale. Without it the shape would be scaled away from the raster it is meant to describe.
TGFX_TEST_PRIVATE(LayerTest, GeometryShapeUndoesTheRecordingScale) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  RegionSpec region = {};
  region.bounds = Rect::MakeXYWH(10.0f, 10.0f, 100.0f, 80.0f);
  region.fill = RegionFillKind::Opaque;
  auto panel = MakePanel({region}, {MakeGeometryGlass(0, 50, 100, 0, 0)});

  auto identityProbe = ProbeGeometry(context, panel);
  auto scaledProbe = ProbeGeometry(context, panel, Matrix::MakeScale(2.0f));
  ASSERT_TRUE(identityProbe.shape.has_value());
  ASSERT_TRUE(scaledProbe.shape.has_value())
      << "the content scale must not change whether the recording resolves a shape";
  ASSERT_TRUE(identityProbe.shape->shape != nullptr && scaledProbe.shape->shape != nullptr);
  EXPECT_EQ(scaledProbe.shape->shape->getBounds(), identityProbe.shape->shape->getBounds())
      << "the shape lives in layer coordinates, so it does not grow with the content scale";
}

// The geometry is not painted content: it is recorded as an unpainted entry, so nothing draws
// it, and the content bounds -- which is what mapContentBoundsToImage() hands to filters and style
// sources -- must leave it out.
TGFX_TEST_PRIVATE(LayerTest, GeometryStaysOutOfContentBounds) {
  const Rect bareBounds = Rect::MakeXYWH(20.0f, 30.0f, 40.0f, 25.0f);
  RegionSpec bareGeometry = {};
  bareGeometry.bounds = bareBounds;
  auto layer = MakePanel({bareGeometry}, {MakeGeometryGlass(0, 50, 100, 0, 0)});

  TGFX_PRIVATE_ACCESS(
      EXPECT_TRUE(layer->computeBounds(Matrix3D::I(), false, true).isEmpty())
          << "content bounds must not include geometry that nothing draws, or the filter mapping "
             "would be computed against pixels that are never rendered";
      EXPECT_EQ(layer->computeBounds(Matrix3D::I(), false, false), bareBounds)
      << "the bounds the style has to cover still include the geometry";)
}

// A Repeater clones the accumulated geometries and remaps each painter onto its own clones, so the
// bare-geometry difference has to be taken over the clones: a painter matched by pointer identity
// would otherwise leave every clone counted as bare.
TGFX_TEST_PRIVATE(LayerTest, RepeaterKeepsTheBareGeometryDifference) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const Rect paintedBounds = Rect::MakeLTRB(20.0f, 20.0f, 50.0f, 50.0f);
  const Rect bareBounds = Rect::MakeLTRB(20.0f, 70.0f, 50.0f, 100.0f);
  constexpr float RepeatOffset = 80.0f;

  auto repeater = Repeater::Make();
  repeater->setCopies(2.0f);
  repeater->setOffset(0.0f);
  repeater->setAnchor({0.0f, 0.0f});
  repeater->setPosition({RepeatOffset, 0.0f});
  repeater->setOrder(RepeaterOrder::AboveOriginal);

  auto group = VectorGroup::Make();
  // The fill captures one geometry, then the bare rectangle joins it, and the Repeater clones both
  // while remapping the painter onto the clones it captured.
  group->setElements({MakeRectElement(paintedBounds),
                      FillStyle::Make(SolidColor::Make(Color::White())),
                      MakeRectElement(bareBounds), repeater});
  auto layer = VectorLayer::Make();
  layer->setContents({group});
  layer->setLayerStyles({MakeGeometryGlass(0, 50, 100, 0, 0)});

  auto alpha = RasterizeGeometryAlpha(context, layer);
  ASSERT_FALSE(alpha.empty());
  EXPECT_EQ(CountUnpaintedEntries(layer), 2)
      << "only the two replicated bare rectangles differ; four would mean the painters no longer "
         "match their clones";
  EXPECT_EQ(GeometryAlphaAt(alpha, 35, 85), 255) << "the first bare clone is covered";
  EXPECT_EQ(GeometryAlphaAt(alpha, 115, 85), 255) << "the replicated bare clone is covered";
  EXPECT_EQ(GeometryAlphaAt(alpha, 35, 35), 255) << "the painted clone stays in the contour";
  EXPECT_EQ(GeometryAlphaAt(alpha, 165, 165), 0) << "outside every clone stays uncovered";
}

// MergePath drops the accumulated painters and adds the merged shape as a new geometry. A painter
// after it captures that shape, so the merged result is content and must not also be recorded as
// bare geometry; a painter before it is dropped, so then nothing paints the merged shape and the
// bare channel is the only one that can describe it.
TGFX_TEST_PRIVATE(LayerTest, MergePathResultStaysWithItsPainter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const Rect firstRect = Rect::MakeLTRB(10.0f, 10.0f, 70.0f, 70.0f);
  const Rect secondRect = Rect::MakeLTRB(50.0f, 50.0f, 110.0f, 110.0f);
  auto makePanel = [&](bool mergeBeforeFill) {
    auto mergePath = MergePath::Make();
    mergePath->setMode(MergePathOp::Union);
    std::vector<std::shared_ptr<VectorElement>> elements = {MakeRectElement(firstRect),
                                                            MakeRectElement(secondRect)};
    auto fill = FillStyle::Make(SolidColor::Make(Color::White()));
    if (mergeBeforeFill) {
      elements.push_back(mergePath);
      elements.push_back(fill);
    } else {
      elements.push_back(fill);
      elements.push_back(mergePath);
    }
    auto group = VectorGroup::Make();
    group->setElements(std::move(elements));
    auto layer = VectorLayer::Make();
    layer->setContents({group});
    layer->setLayerStyles({MakeGeometryGlass(0, 50, 100, 0, 0)});
    return layer;
  };

  auto merged = makePanel(true);
  auto alpha = RasterizeGeometryAlpha(context, merged);
  ASSERT_FALSE(alpha.empty());
  EXPECT_EQ(CountUnpaintedEntries(merged), 0)
      << "the fill after the merge captures the merged shape, so it is content and must not also "
         "be recorded as bare geometry";
  // Asserted on the raster as well: the merged outline covers the union and keeps out of the corner
  // that neither square covers.
  EXPECT_EQ(GeometryAlphaAt(alpha, 30, 30), 255) << "inside the first rectangle only";
  EXPECT_EQ(GeometryAlphaAt(alpha, 90, 90), 255) << "inside the second rectangle only";
  EXPECT_EQ(GeometryAlphaAt(alpha, 60, 60), 255) << "inside the overlap";
  EXPECT_EQ(GeometryAlphaAt(alpha, 90, 30), 0) << "outside the union";

  auto mergeAfterFill = makePanel(false);
  auto mergeAfterFillAlpha = RasterizeGeometryAlpha(context, mergeAfterFill);
  ASSERT_EQ(mergeAfterFillAlpha.size(), alpha.size());
  EXPECT_EQ(CountUnpaintedEntries(mergeAfterFill), 1)
      << "MergePath clears the painters, so nothing paints the merged shape there";
  int differingPixels = 0;
  for (size_t i = 0; i < alpha.size(); i++) {
    if (alpha[i] != mergeAfterFillAlpha[i]) {
      differingPixels++;
    }
  }
  EXPECT_EQ(differingPixels, 0)
      << "both orders must describe the same union, so the difference above is only about which "
         "channel carries it";
}

// ===== Text without a painter is geometry =====
//
// A Vector layer holding only text has no painter at all, so Geometry::getShape() turns the text
// blob into glyph outlines and those outlines are bare geometry: the layer's geometry, and with
// it the coverage a Geometry-source style such as Glass draws from.
TGFX_TEST_PRIVATE(LayerTest, GeometryIncludesTextGeometry) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr) << "the glyph outlines come from the font, so this is required";
  auto blob = TextBlob::MakeFrom("TGFX", Font(typeface, 36.0f));
  ASSERT_TRUE(blob != nullptr);
  auto text = Text::Make(blob);
  ASSERT_TRUE(text != nullptr);
  // Placed well inside the probe surface: a glyph above y=0 would be cropped by the probe read.
  text->setPosition({20.0f, 100.0f});

  auto makePanel = [&](bool withGlass) {
    auto layer = VectorLayer::Make();
    // Text only, no FillStyle: nothing captures the blob, so no painter owns the glyph outlines.
    layer->setContents({text});
    if (withGlass) {
      layer->setLayerStyles({MakeGeometryGlass(0, 50, 100, 0, 0)});
    }
    return layer;
  };

  auto panel = makePanel(true);
  TGFX_PRIVATE_ACCESS(
      DrawArgs drawArgs(context); auto source = panel->getLayerStyleSource(drawArgs, Matrix::I());
      ASSERT_TRUE(source != nullptr); ASSERT_TRUE(source->groups[1] != nullptr);
      ASSERT_TRUE(source->groups[1]->geometry != nullptr)
      << "text without a painter is still the layer's geometry";
      EXPECT_TRUE(source->groups[1]->geometry->image() != nullptr);
      EXPECT_TRUE(source->groups[1]->geometry->shape().has_value())
      << "the glyph outlines fit a single StyledShape, so Glass takes the SDF path";)

  // The coverage is the glyph shapes: something is covered, and it is not the whole probe surface.
  auto alpha = RasterizeGeometryAlpha(context, panel);
  ASSERT_FALSE(alpha.empty());
  int covered = 0;
  for (auto value : alpha) {
    covered += value == 255 ? 1 : 0;
  }
  EXPECT_GT(covered, 0) << "the glyph outline must cover something";
  EXPECT_LT(covered * 2, static_cast<int>(alpha.size()))
      << "a text outline covers its glyphs, not the whole surface; covered=" << covered;

  // Recorded only for a style that consumes the geometry: a style drawing something else still
  // resolves a source, but hands no geometry over, so the recording above is not unconditional.
  auto shadowed = makePanel(false);
  // A painted group keeps the shared content non-empty, since a style drawing something else still
  // records it. The fill scopes to its own group, so the glyph outlines above stay bare geometry.
  auto paintedGroup = VectorGroup::Make();
  paintedGroup->setElements({MakeRectElement(Rect::MakeXYWH(10.0f, 10.0f, 30.0f, 30.0f)),
                             FillStyle::Make(SolidColor::Make(Color::Red()))});
  shadowed->setContents({text, paintedGroup});
  shadowed->setLayerStyles({DropShadowStyle::Make(-20, -20, 0, 0, Color::Black())});
  std::unique_ptr<LayerStyleSource> shadowSource = nullptr;
  TGFX_PRIVATE_ACCESS(DrawArgs shadowArgs(context);
                      shadowSource = shadowed->getLayerStyleSource(shadowArgs, Matrix::I());)
  ASSERT_TRUE(shadowSource != nullptr) << "a non-geometry style still resolves its own source";
  for (const auto& group : shadowSource->groups) {
    EXPECT_TRUE(group == nullptr || group->geometry == nullptr)
        << "only a style declaring the layer's geometry may record it";
  }
}

// ===== Own geometry under a transform-only change =====
//
// A layer whose style consumes bare geometry paints nothing in the normal draw, yet getContent()
// is not null: finishRecording() returns a content whose only entry is unpainted. Only the
// includeUnpaintedGeometry bounds query in Layer::updateRenderBounds() can give the layer bounds.
// Moving the layer only marks the transform dirty, so without that recompute the bounds stay where
// they were: the old region is never invalidated and Layer::draw culls the layer against its stale
// renderBounds.

constexpr int GeometryMoveSurfaceWidth = 300;
constexpr int GeometryMoveSurfaceHeight = 200;
constexpr float GeometryMoveDistance = 100.0f;
// Wide enough for the glass to cover the sampled pixel with room to spare, narrow enough that the
// start and the moved position both stay inside the surface without overlapping.
constexpr float GeometryPanelSize = 40.0f;

static Rect GeometryPanelRect(float left, float top) {
  return Rect::MakeXYWH(left, top, GeometryPanelSize, GeometryPanelSize);
}

// The backdrop the glass frosts: an opaque red field crossed by dark red stripes. A uniform field
// would make the frost invisible -- blurring a constant color changes nothing -- so the stripes are
// what lets a pixel read tell a frosted region from the plain backdrop.
static std::shared_ptr<Layer> MakeStripedRedBackdrop() {
  const Rect fullRect = Rect::MakeWH(GeometryMoveSurfaceWidth, GeometryMoveSurfaceHeight);
  auto base = VectorGroup::Make();
  base->setElements(
      {MakeRectElement(fullRect), FillStyle::Make(SolidColor::Make(Color::FromRGBA(255, 0, 0)))});
  std::vector<std::shared_ptr<VectorElement>> stripes = {};
  for (float left = 0.0f; left + 12.0f <= GeometryMoveSurfaceWidth; left += 24.0f) {
    stripes.push_back(MakeRectElement(
        Rect::MakeXYWH(left, 0.0f, 12.0f, static_cast<float>(GeometryMoveSurfaceHeight))));
  }
  stripes.push_back(FillStyle::Make(SolidColor::Make(Color::FromRGBA(48, 0, 0))));
  auto stripeGroup = VectorGroup::Make();
  stripeGroup->setElements(std::move(stripes));
  auto backdrop = VectorLayer::Make();
  backdrop->setContents({base, stripeGroup});
  return backdrop;
}

// The layer under test: one rectangle and no painter at all. Its content is not empty -- the
// rectangle is recorded as an unpainted entry -- but nothing in it paints in the normal draw, so
// the GlassStyle is the only thing that makes it paint anything.
static std::shared_ptr<VectorLayer> MakeBareGeometryGlassLayer(const Rect& bounds) {
  auto group = VectorGroup::Make();
  group->setElements({MakeRectElement(bounds)});
  auto layer = VectorLayer::Make();
  layer->setContents({group});
  layer->setLayerStyles({MakeGeometryGlass(0, 50, 100, 0, 0)});
  return layer;
}

static std::optional<uint32_t> ReadPixelValue(Surface* surface, int x, int y) {
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  uint32_t pixel = 0;
  if (!surface->readPixels(info, &pixel, x, y)) {
    return std::nullopt;
  }
  return pixel;
}

static int MaxChannelDifference(uint32_t first, uint32_t second) {
  int maxDiff = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    auto firstChannel = static_cast<int>((first >> shift) & 0xFF);
    auto secondChannel = static_cast<int>((second >> shift) & 0xFF);
    auto diff =
        firstChannel > secondChannel ? firstChannel - secondChannel : secondChannel - firstChannel;
    maxDiff = std::max(maxDiff, diff);
  }
  return maxDiff;
}

TGFX_TEST_PRIVATE(LayerTest, GeometryLayerMovesItsRenderBounds) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, GeometryMoveSurfaceWidth, GeometryMoveSurfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  const auto panelRect = GeometryPanelRect(20.0f, 20.0f);

  DisplayList displayList;
  displayList.root()->addChild(MakeStripedRedBackdrop());
  auto layer = MakeBareGeometryGlassLayer(panelRect);
  displayList.root()->addChild(layer);
  displayList.render(surface.get());

  Rect before = {};
  TGFX_PRIVATE_ACCESS(before = layer->renderBounds;)
  ASSERT_FALSE(before.isEmpty()) << "the unpainted-geometry bounds query must give the layer "
                                    "bounds, otherwise the move below proves nothing";

  layer->setPosition({GeometryMoveDistance, 0.0f});
  displayList.render(surface.get(), false);

  Rect after = {};
  TGFX_PRIVATE_ACCESS(after = layer->renderBounds;)
  EXPECT_NEAR(after.left, before.left + GeometryMoveDistance, 0.5f)
      << "moving a bare-geometry layer must move its render bounds; a stale rect leaves the old "
         "region dirty and makes Layer::draw cull the layer at its new place. before="
      << before.left << " after=" << after.left;
  EXPECT_NEAR(after.top, before.top, 0.5f)
      << "a horizontal move must not shift the bounds vertically";
  EXPECT_NEAR(after.width(), before.width(), 0.5f) << "the move must not resize the bounds";
  EXPECT_NEAR(after.height(), before.height(), 0.5f) << "the move must not resize the bounds";
}

TGFX_TEST_PRIVATE(LayerTest, GeometryLayerFollowsParentTransform) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, GeometryMoveSurfaceWidth, GeometryMoveSurfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  const auto panelRect = GeometryPanelRect(20.0f, 20.0f);

  DisplayList displayList;
  displayList.root()->addChild(MakeStripedRedBackdrop());
  auto parent = Layer::Make();
  auto layer = MakeBareGeometryGlassLayer(panelRect);
  parent->addChild(layer);
  displayList.root()->addChild(parent);
  displayList.render(surface.get());

  Rect before = {};
  TGFX_PRIVATE_ACCESS(before = layer->renderBounds;)
  ASSERT_FALSE(before.isEmpty()) << "the geometry gate must give the child bounds";

  // The child's own transform is untouched, so its bounds have to follow through childForceDirty.
  parent->setMatrix(Matrix::MakeTrans(GeometryMoveDistance, 0.0f));
  displayList.render(surface.get(), false);

  Rect after = {};
  TGFX_PRIVATE_ACCESS(after = layer->renderBounds;)
  EXPECT_NEAR(after.left, before.left + GeometryMoveDistance, 0.5f)
      << "a parent transform must reach the bare-geometry child's render bounds. before="
      << before.left << " after=" << after.left;
  EXPECT_NEAR(after.top, before.top, 0.5f) << "a horizontal parent move must not shift the bounds";
  EXPECT_NEAR(after.width(), before.width(), 0.5f) << "the parent move must not resize the bounds";
  EXPECT_NEAR(after.height(), before.height(), 0.5f)
      << "the parent move must not resize the bounds";
}

TGFX_TEST_PRIVATE(LayerTest, GeometryLayerRedrawsAfterMove) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  const auto panelRect = GeometryPanelRect(20.0f, 20.0f);
  // Big enough that a frosted stripe pattern cannot be mistaken for the plain backdrop.
  constexpr int threshold = 8;

  // Reference: the same backdrop with no glass at all, so "the glass was drawn" is measured against
  // the backdrop rather than against a second glass configuration.
  auto referenceSurface =
      Surface::Make(context, GeometryMoveSurfaceWidth, GeometryMoveSurfaceHeight);
  ASSERT_TRUE(referenceSurface != nullptr);
  {
    DisplayList referenceList;
    referenceList.root()->addChild(MakeStripedRedBackdrop());
    referenceList.render(referenceSurface.get());
  }

  auto surface = Surface::Make(context, GeometryMoveSurfaceWidth, GeometryMoveSurfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  DisplayList displayList;
  displayList.root()->addChild(MakeStripedRedBackdrop());
  auto layer = MakeBareGeometryGlassLayer(panelRect);
  displayList.root()->addChild(layer);
  displayList.render(surface.get());

  const int startX = static_cast<int>(panelRect.centerX());
  const int centerY = static_cast<int>(panelRect.centerY());
  const int movedX = startX + static_cast<int>(GeometryMoveDistance);
  auto referenceStart = ReadPixelValue(referenceSurface.get(), startX, centerY);
  auto referenceMoved = ReadPixelValue(referenceSurface.get(), movedX, centerY);
  auto startBefore = ReadPixelValue(surface.get(), startX, centerY);
  auto movedBefore = ReadPixelValue(surface.get(), movedX, centerY);
  ASSERT_TRUE(referenceStart.has_value() && referenceMoved.has_value() && startBefore.has_value() &&
              movedBefore.has_value());
  EXPECT_GT(MaxChannelDifference(*startBefore, *referenceStart), threshold)
      << "fixture control: the glass must be visible at the start position in the first frame";
  EXPECT_LE(MaxChannelDifference(*movedBefore, *referenceMoved), threshold)
      << "fixture control: the moved position must start out as plain backdrop";

  // Incremental frame: only the dirty regions are replayed, so the layer has to be drawn at its new
  // place AND the backdrop has to be restored where it used to be.
  layer->setPosition({GeometryMoveDistance, 0.0f});
  displayList.render(surface.get(), false);

  auto startAfter = ReadPixelValue(surface.get(), startX, centerY);
  auto movedAfter = ReadPixelValue(surface.get(), movedX, centerY);
  ASSERT_TRUE(startAfter.has_value() && movedAfter.has_value());
  EXPECT_GT(MaxChannelDifference(*movedAfter, *referenceMoved), threshold)
      << "the moved layer must be drawn at its new position; an equal backdrop read means the new "
         "region was never redrawn";
  EXPECT_LE(MaxChannelDifference(*startAfter, *referenceStart), threshold)
      << "the region the layer left must be repainted as plain backdrop, so a stale render bounds "
         "left the old glass behind";
}

// ===== An emptied layer must not get bounds back from its style outset =====
//
// updateRenderBounds() maps the content bounds through the layer's filters and styles, and a style
// outsets whatever it is handed. Mapping the empty rect of a layer that paints nothing turns empty
// into non-empty, so the layer would keep a render bounds and dirty a region although it has
// nothing to draw.

TGFX_TEST_PRIVATE(LayerTest, EmptyContentDoesNotGetStyleOutsetBounds) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);

  DisplayList displayList;
  auto group = VectorGroup::Make();
  group->setElements({MakeRectElement(Rect::MakeXYWH(20.0f, 20.0f, 40.0f, 40.0f)),
                      FillStyle::Make(SolidColor::Make(Color::White()))});
  auto layer = VectorLayer::Make();
  layer->setContents({group});
  // DropShadow outsets its bounds by its blur radius, so an empty content bounds reaches the
  // transformer and comes back non-empty. Glass cannot show this: its filterBounds is the identity.
  layer->setLayerStyles({DropShadowStyle::Make(0.0f, 0.0f, 8.0f, 8.0f, Color::Black())});
  displayList.root()->addChild(layer);
  displayList.render(surface.get());

  Rect before = {};
  TGFX_PRIVATE_ACCESS(before = layer->renderBounds;)
  ASSERT_FALSE(before.isEmpty()) << "fixture control: the painted layer must have render bounds";

  layer->setContents({});
  // updateRenderBounds() is the step of RootLayer::updateDirtyRegions() that recomputes bounds, and
  // the dirty list is only drained by updateDirtyRegions(), so calling it directly tells what the
  // emptied layer invalidated without consuming the list first.
  std::vector<Rect> dirty = {};
  Rect after = {};
  TGFX_PRIVATE_ACCESS(displayList._root->updateRenderBounds();
                      dirty = displayList._root->currentDirtyRects(); after = layer->renderBounds;)
  EXPECT_TRUE(after.isEmpty())
      << "a layer with no content must not keep the bounds the style outset gave it, otherwise it "
         "is drawn and culled against a footprint it does not have. after="
      << after.left << "," << after.top << " " << after.width() << "x" << after.height();
  ASSERT_FALSE(dirty.empty()) << "the region the panel used to cover must still be invalidated";
  for (const auto& rect : dirty) {
    EXPECT_TRUE(before.contains(rect))
        << "an emptied layer may only invalidate the region it used to cover. A rect outside that "
           "region dirties pixels the layer never drew. dirty="
        << rect.left << "," << rect.top << " " << rect.width() << "x" << rect.height()
        << " before=" << before.left << "," << before.top << " " << before.width() << "x"
        << before.height();
  }

  // The same must hold for a real incremental frame: nothing may bring the bounds back.
  displayList.render(surface.get(), false);
  TGFX_PRIVATE_ACCESS(after = layer->renderBounds;)
  EXPECT_TRUE(after.isEmpty()) << "an incremental frame must not resurrect the dropped bounds";
}

// ===== A geometry no paint captured is an ordinary content entry =====
//
// Such a geometry is recorded as content without a paint instead of living in a parallel list on the
// layer. It paints nothing and contributes no shared contour, so these cases assert both sides: the
// migration must not start drawing it, and the queries that do ask for it must still find it.

// Reads the alpha channel of a square surface.
static std::vector<int> ReadAlphaRaster(const std::shared_ptr<Surface>& surface) {
  std::vector<int> raster = {};
  if (surface == nullptr) {
    return raster;
  }
  auto size = surface->width();
  if (surface->height() != size) {
    return raster;
  }
  auto info = ImageInfo::Make(size, size, ColorType::RGBA_8888, AlphaType::Premultiplied);
  std::vector<uint32_t> pixels(static_cast<size_t>(size) * static_cast<size_t>(size));
  if (!surface->readPixels(info, pixels.data())) {
    return raster;
  }
  raster.reserve(pixels.size());
  for (auto pixel : pixels) {
    raster.push_back(static_cast<int>((pixel >> 24) & 0xFF));
  }
  return raster;
}

// Alpha of a square raster at (x, y), in layer-local coordinates. Returns -1 when the raster does not
// describe a square of that size.
static int RasterAlphaAt(const std::vector<int>& raster, int size, int x, int y) {
  if (x < 0 || y < 0 || x >= size || y >= size ||
      raster.size() != static_cast<size_t>(size) * static_cast<size_t>(size)) {
    return -1;
  }
  return raster[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)];
}

// Records the contour shared with layer styles and masks of `layer` onto a probe surface, in
// layer-local coordinates, and returns its alpha raster. A contour that records nothing leaves no
// image behind, which is the same coverage as an image of zeros.
static std::vector<int> RasterizeSharedContourAlpha(Context* context,
                                                    const std::shared_ptr<Layer>& layer) {
  auto surface = Surface::Make(context, GeometryProbeSize, GeometryProbeSize);
  if (surface == nullptr) {
    return {};
  }
  std::shared_ptr<Image> image = nullptr;
  Point offset = Point::Zero();
  TGFX_PRIVATE_ACCESS(DrawArgs drawArgs(context);
                      image = layer->getContentContourImage(drawArgs, 1.0f, &offset, nullptr);)
  if (image == nullptr) {
    return std::vector<int>(static_cast<size_t>(GeometryProbeSize) * GeometryProbeSize, 0);
  }
  // The image is cropped to the outline's rounded-out bounds, so its recorded offset puts the raster
  // back into layer-local coordinates.
  surface->getCanvas()->drawImage(image, offset.x, offset.y);
  return ReadAlphaRaster(surface);
}

// Draws the own content of `layer` as a contour with the requested caliber onto a probe surface, in
// layer-local coordinates, and returns its alpha raster.
static std::vector<int> RasterizeContentContourAlpha(Context* context,
                                                     const std::shared_ptr<Layer>& layer,
                                                     bool includeUnpaintedGeometry) {
  std::vector<int> raster = {};
  auto surface = Surface::Make(context, GeometryProbeSize, GeometryProbeSize);
  if (surface == nullptr) {
    return raster;
  }
  LayerContent* content = nullptr;
  TGFX_PRIVATE_ACCESS(content = layer->getContent());
  if (content == nullptr) {
    return raster;
  }
  content->drawContour(surface->getCanvas(), false, includeUnpaintedGeometry);
  return ReadAlphaRaster(surface);
}

TGFX_TEST_PRIVATE(LayerTest, PaintlessEntryNeverDraws) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const Rect paintedBounds = Rect::MakeXYWH(20.0f, 20.0f, 60.0f, 40.0f);
  const Rect bareBounds = Rect::MakeXYWH(110.0f, 20.0f, 60.0f, 40.0f);
  auto buildLayer = [&](bool withBareGeometry) {
    std::vector<std::shared_ptr<VectorElement>> elements = {
        MakeRectElement(paintedBounds), FillStyle::Make(SolidColor::Make(Color::White()))};
    if (withBareGeometry) {
      elements.push_back(MakeRectElement(bareBounds));
    }
    auto group = VectorGroup::Make();
    group->setElements(std::move(elements));
    auto layer = VectorLayer::Make();
    layer->setContents({group});
    return layer;
  };
  auto render = [context](const std::shared_ptr<Layer>& layer) {
    auto surface = Surface::Make(context, GeometryProbeSize, GeometryProbeSize);
    if (surface == nullptr) {
      return surface;
    }
    DisplayList displayList;
    displayList.root()->addChild(layer);
    displayList.render(surface.get());
    return surface;
  };

  auto paintedLayer = buildLayer(false);
  auto bareLayer = buildLayer(true);
  // Fixture control: the second rectangle really is recorded, and as an entry without a paint.
  EXPECT_EQ(CountUnpaintedEntries(paintedLayer), 0);
  EXPECT_EQ(CountUnpaintedEntries(bareLayer), 1);

  auto painted = render(paintedLayer);
  auto withBare = render(bareLayer);
  ASSERT_TRUE(painted != nullptr && withBare != nullptr);
  EXPECT_EQ(CountDifferingPixels(painted.get(), withBare.get(),
                                 Rect::MakeWH(GeometryProbeSize, GeometryProbeSize), 0),
            0)
      << "a geometry no paint captured must not add a single pixel to the normal draw";
  auto paintedPixel = ReadPixelValue(painted.get(), static_cast<int>(paintedBounds.centerX()),
                                     static_cast<int>(paintedBounds.centerY()));
  ASSERT_TRUE(paintedPixel.has_value());
  EXPECT_GT(static_cast<int>((*paintedPixel >> 24) & 0xFF), 0)
      << "fixture control: the painted rectangle must be visible, otherwise two empty renders "
         "would "
         "compare equal";
}

TGFX_TEST_PRIVATE(LayerTest, SharedContourExcludesPaintless) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const Rect rect = Rect::MakeXYWH(60.0f, 60.0f, 80.0f, 80.0f);
  // The geometry lives in the child; the parent only records the contour its styles share.
  auto makeParent = [&rect](bool withAlphaZeroPainter) {
    std::vector<std::shared_ptr<VectorElement>> elements = {MakeRectElement(rect)};
    if (withAlphaZeroPainter) {
      elements.push_back(MakeZeroAlphaFill());
    }
    auto group = VectorGroup::Make();
    group->setElements(std::move(elements));
    auto child = VectorLayer::Make();
    child->setContents({group});
    auto parent = Layer::Make();
    parent->addChild(child);
    return parent;
  };

  auto alphaZero = RasterizeSharedContourAlpha(context, makeParent(true));
  auto paintless = RasterizeSharedContourAlpha(context, makeParent(false));
  ASSERT_FALSE(alphaZero.empty());
  ASSERT_EQ(alphaZero.size(), paintless.size());
  EXPECT_EQ(RasterAlphaAt(alphaZero, GeometryProbeSize, 100, 100), 255)
      << "control: a painter with alpha 0 stays in the contour shared with styles and masks";
  EXPECT_EQ(RasterAlphaAt(paintless, GeometryProbeSize, 100, 100), 0)
      << "a geometry no paint captured must stay out of that contour, otherwise a bare descendant "
         "starts covering the parent's style output";
  EXPECT_EQ(RasterAlphaAt(paintless, GeometryProbeSize, 20, 20), 0)
      << "the whole contour must stay empty when only bare geometry is present";
}

TGFX_TEST_PRIVATE(LayerTest, ContourRepresentativeFollowsCaliber) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const Rect rect = Rect::MakeXYWH(60.0f, 60.0f, 80.0f, 80.0f);
  // The same outline recorded as a bare entry and as a painter whose alpha is 0. Grouping keeps the
  // two apart, so this fixture cannot produce one group holding both kinds of entry: it pins the
  // invariant the caliber filter relies on instead. Two groups of one entry build no representative
  // table at all, and the painted caliber draws the outline through the alpha-0 painter alone.
  {
    auto group = VectorGroup::Make();
    group->setElements({MakeRectElement(rect), MakeZeroAlphaFill(), MakeRectElement(rect)});
    auto layer = VectorLayer::Make();
    layer->setContents({group});
    EXPECT_EQ(CountUnpaintedEntries(layer), 1);
    TGFX_PRIVATE_ACCESS(
        auto content = layer->getContent(); ASSERT_NE(content, nullptr);
        ASSERT_EQ(content->type(), LayerContent::Type::Compose)
        << "the bare entry and the painted one must stay separate, so the recording is a "
           "ComposeContent";
        auto compose = static_cast<const ComposeContent*>(content);
        EXPECT_TRUE(compose->contours.empty())
        << "a bare entry must not share a group with a painted one, so the two entries above stay "
           "apart and no representative table is built; an empty table is what makes the queries "
           "read every entry, and building one here would mean a bare duplicate could take the "
           "representative slot away from the painted entry";)
    auto paintedCaliber = RasterizeContentContourAlpha(context, layer, false);
    auto allCaliber = RasterizeContentContourAlpha(context, layer, true);
    EXPECT_EQ(RasterAlphaAt(paintedCaliber, GeometryProbeSize, 100, 100), 255)
        << "the painter whose alpha is 0 still draws the contour of the painted caliber";
    EXPECT_EQ(RasterAlphaAt(allCaliber, GeometryProbeSize, 100, 100), 255);
  }
  // Only bare geometry, two entries sharing one outline. A representative table exists here, and a
  // selection that comes out empty has to mean "draw nothing" rather than "read every entry": the
  // latter would leak the bare geometry into the queries that exclude it.
  {
    Path triangle = {};
    triangle.moveTo(60.0f, 60.0f);
    triangle.lineTo(140.0f, 60.0f);
    triangle.lineTo(100.0f, 140.0f);
    triangle.close();
    auto makeTriangle = [&triangle]() {
      auto shape = ShapePath::Make();
      shape->setPath(triangle);
      return shape;
    };
    auto group = VectorGroup::Make();
    group->setElements({makeTriangle(), makeTriangle()});
    auto layer = VectorLayer::Make();
    layer->setContents({group});
    EXPECT_EQ(CountUnpaintedEntries(layer), 2)
        << "the fixture needs two entries sharing one outline, or no representative table exists";
    // Asserted on the table itself: every query already excludes a bare entry on its own, so a
    // wrong representative is invisible in pixels here and only shows up as the painted caliber
    // borrowing a representative that paints nothing.
    TGFX_PRIVATE_ACCESS(
        auto content = layer->getContent(); ASSERT_NE(content, nullptr);
        ASSERT_EQ(content->type(), LayerContent::Type::Compose)
        << "two bare entries must stay separate entries, so the recording is a ComposeContent";
        auto compose = static_cast<const ComposeContent*>(content);
        ASSERT_FALSE(compose->contours.empty())
        << "two entries sharing one outline must build a representative table";
        EXPECT_EQ(compose->contours.size(), 1u)
        << "the caliber that includes bare geometry keeps one representative for the outline";
        ASSERT_TRUE(compose->paintedContours.has_value())
        << "the painted caliber selects nothing here, which must stay distinguishable from having "
           "no table at all";
        EXPECT_TRUE(compose->paintedContours->empty())
        << "the caliber that excludes bare geometry keeps no representative for this outline";
        EXPECT_FALSE(compose->contourEqualsOpaqueContent(true))
        << "a bare entry is covered by the contour only when the query includes it";)
    auto paintedCaliber = RasterizeContentContourAlpha(context, layer, false);
    auto allCaliber = RasterizeContentContourAlpha(context, layer, true);
    EXPECT_EQ(RasterAlphaAt(allCaliber, GeometryProbeSize, 100, 100), 255)
        << "the caliber that includes bare geometry must draw it";
    EXPECT_EQ(RasterAlphaAt(paintedCaliber, GeometryProbeSize, 100, 100), 0)
        << "an empty selection means nothing is drawn, not that every entry is read";
  }
}

// The other side of the caliber split: two painted entries sharing one outline select the same
// representative in both calibers, so the second table must stay unmaterialized. A nullopt read as
// "no table at all" would send the painted caliber back to reading every entry.
TGFX_TEST_PRIVATE(LayerTest, ContourRepresentativeSharedWhenCalibersAgree) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Path triangle = {};
  triangle.moveTo(60.0f, 60.0f);
  triangle.lineTo(140.0f, 60.0f);
  triangle.lineTo(100.0f, 140.0f);
  triangle.close();
  auto makeTriangle = [&triangle]() {
    auto shape = ShapePath::Make();
    shape->setPath(triangle);
    return shape;
  };
  // Two shapes rather than two rectangles: consecutive rectangles sharing one paint batch into a
  // single entry, and one entry leaves nothing to collapse into a representative.
  auto group = VectorGroup::Make();
  group->setElements(
      {makeTriangle(), makeTriangle(), FillStyle::Make(SolidColor::Make(Color::White()))});
  auto layer = VectorLayer::Make();
  layer->setContents({group});
  EXPECT_EQ(CountUnpaintedEntries(layer), 0)
      << "the fill must paint both shapes, or the painted caliber has nothing to select";

  TGFX_PRIVATE_ACCESS(
      auto content = layer->getContent(); ASSERT_NE(content, nullptr);
      ASSERT_EQ(content->type(), LayerContent::Type::Compose)
      << "the two shapes must stay separate entries, or no representative table is built";
      auto compose = static_cast<const ComposeContent*>(content);
      ASSERT_EQ(compose->contents.size(), 2u)
      << "the fixture needs one entry per shape, both painted by the fill";
      ASSERT_FALSE(compose->contours.empty())
      << "two painted entries sharing one outline must build a representative table";
      EXPECT_EQ(compose->contours.size(), 1u);
      EXPECT_FALSE(compose->paintedContours.has_value())
      << "both calibers select the same representative, so no second table is needed";
      // Read through the selection the draw code uses: an unmaterialized painted table has to mean
      // "the same as contours", not "no table at all", which would send the painted caliber back to
      // reading every entry.
      EXPECT_EQ(compose->selectedContours(false), &compose->contours)
      << "the painted caliber must reuse the first table when both calibers agree";)

  // Pixel control for the shared table: it must cover the outline in both calibers and nothing
  // outside it. Two opaque draws over one outline are indistinguishable here, so the single draw is
  // pinned by the table assertions above, not by these pixels.
  auto paintedCaliber = RasterizeContentContourAlpha(context, layer, false);
  auto allCaliber = RasterizeContentContourAlpha(context, layer, true);
  EXPECT_EQ(RasterAlphaAt(paintedCaliber, GeometryProbeSize, 100, 100), 255)
      << "the shared representative still draws the outline for the painted caliber";
  EXPECT_EQ(RasterAlphaAt(allCaliber, GeometryProbeSize, 100, 100), 255)
      << "and for the caliber that includes bare geometry";
  EXPECT_EQ(RasterAlphaAt(paintedCaliber, GeometryProbeSize, 20, 20), 0)
      << "the shared representative must not grow beyond the outline";
  EXPECT_EQ(RasterAlphaAt(allCaliber, GeometryProbeSize, 20, 20), 0);
}

TGFX_TEST_PRIVATE(LayerTest, PaintlessEntryNotHitTestable) {
  const Rect paintedBounds = Rect::MakeXYWH(20.0f, 20.0f, 60.0f, 40.0f);
  const Rect bareBounds = Rect::MakeXYWH(110.0f, 110.0f, 60.0f, 40.0f);
  auto group = VectorGroup::Make();
  group->setElements({MakeRectElement(paintedBounds),
                      FillStyle::Make(SolidColor::Make(Color::White())),
                      MakeRectElement(bareBounds)});
  auto layer = VectorLayer::Make();
  layer->setContents({group});
  EXPECT_EQ(CountUnpaintedEntries(layer), 1);

  // Control: the painted rectangle is hit-testable in both modes, so the misses below are about the
  // entries that carry no paint and not about a mis-built fixture.
  EXPECT_TRUE(layer->hitTestPoint(paintedBounds.centerX(), paintedBounds.centerY()));
  EXPECT_TRUE(layer->hitTestPoint(paintedBounds.centerX(), paintedBounds.centerY(), true));
  EXPECT_FALSE(layer->hitTestPoint(bareBounds.centerX(), bareBounds.centerY()))
      << "a geometry no paint captured must not become clickable through the bounds it lives in";
  EXPECT_FALSE(layer->hitTestPoint(bareBounds.centerX(), bareBounds.centerY(), true))
      << "neither must it be clickable through its shape";
}

// The other side of the gate the test above pins: a style declaring the layer's geometry draws that
// geometry, so the same point query must answer for it too. Glass is such a style, and the layer
// without it keeps the paintless entry out -- that contrast is what makes the gate observable.
TGFX_TEST_PRIVATE(LayerTest, HitTestFollowsTheGeometryCaliber) {
  const Rect bareBounds = Rect::MakeXYWH(60.0f, 60.0f, 80.0f, 40.0f);
  const Rect paintedBounds = Rect::MakeXYWH(60.0f, 140.0f, 80.0f, 40.0f);
  auto makeLayer = [](const Rect& bounds, bool painted, bool withGlass) {
    std::vector<std::shared_ptr<VectorElement>> elements = {MakeRectElement(bounds)};
    if (painted) {
      elements.push_back(FillStyle::Make(SolidColor::Make(Color::White())));
    }
    auto group = VectorGroup::Make();
    group->setElements(std::move(elements));
    auto layer = VectorLayer::Make();
    layer->setContents({group});
    if (withGlass) {
      layer->setLayerStyles({MakeGeometryGlass(0, 50, 100, 0, 0)});
    }
    return layer;
  };

  auto bareWithGlass = makeLayer(bareBounds, false, true);
  EXPECT_EQ(CountUnpaintedEntries(bareWithGlass), 1)
      << "the fixture needs an entry no paint captured, or the caliber is never exercised";
  EXPECT_TRUE(bareWithGlass->hitTestPoint(bareBounds.centerX(), bareBounds.centerY(), false))
      << "a geometry style makes the bare geometry visible, so its bounds must be clickable";
  EXPECT_TRUE(bareWithGlass->hitTestPoint(bareBounds.centerX(), bareBounds.centerY(), true))
      << "and the same geometry must answer the shape query";

  // Control: the same layer without the style. Without the geometry style nothing draws the entry,
  // so it stays unclickable and the hits above are attributable to the style alone.
  auto bareWithoutGlass = makeLayer(bareBounds, false, false);
  EXPECT_EQ(CountUnpaintedEntries(bareWithoutGlass), 1);
  EXPECT_FALSE(bareWithoutGlass->hitTestPoint(bareBounds.centerX(), bareBounds.centerY(), false))
      << "a bare geometry with no style must not be clickable through its bounds";
  EXPECT_FALSE(bareWithoutGlass->hitTestPoint(bareBounds.centerX(), bareBounds.centerY(), true))
      << "nor through its shape";

  // Regression: a painted layer answers both queries with and without the style, so the caliber
  // above only moves the entries no paint captured.
  for (const auto withGlass : {false, true}) {
    auto paintedLayer = makeLayer(paintedBounds, true, withGlass);
    EXPECT_EQ(CountUnpaintedEntries(paintedLayer), 0);
    EXPECT_TRUE(paintedLayer->hitTestPoint(paintedBounds.centerX(), paintedBounds.centerY(), false))
        << "a painted layer must stay clickable through its bounds (glass: " << withGlass << ")";
    EXPECT_TRUE(paintedLayer->hitTestPoint(paintedBounds.centerX(), paintedBounds.centerY(), true))
        << "and through its shape (glass: " << withGlass << ")";
  }
}

// Dirty regions outside the viewport must not rasterize anything: the tiles they reach only get
// invalidated and are rasterized again once the viewport scrolls back, so no stale pixels may be
// left behind after the viewport returns.
TGFX_TEST(LayerTest, TiledDirtyRegionOutsideViewport) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_NE(surface, nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileSize(64);
  // A large tile pool keeps the tiles covering the first viewport cached while the viewport moves,
  // so a missing invalidation shows up as stale pixels instead of a reclaimed tile.
  displayList->setMaxTileCount(400);
  auto solidLayer = SolidLayer::Make();
  solidLayer->setColor(Color::Red());
  solidLayer->setWidth(400);
  solidLayer->setHeight(400);
  displayList->root()->addChild(solidLayer);
  displayList->render(surface.get());
  // Scroll to the right half of the content so the tiles covering x < 200 stay cached off-screen.
  displayList->setContentOffset(-200, 0);
  displayList->render(surface.get());
  // This dirty region covers both the visible tiles and the cached off-screen ones.
  solidLayer->setColor(Color::Blue());
  displayList->render(surface.get());
  // The tile containing x = 192 straddles the left viewport edge, so only its off-screen part
  // (192..200) was reached by the dirty region. It has to be redrawn as well.
  displayList->setContentOffset(-190, 0);
  displayList->render(surface.get());
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  uint32_t pixel = 0;
  ASSERT_TRUE(surface->readPixels(info, &pixel, 2, 10));
  EXPECT_EQ(pixel, 0xFFFF0000u) << "the off-screen part of a straddling tile kept stale content";
  displayList->setContentOffset(0, 0);
  displayList->render(surface.get());
  ASSERT_TRUE(surface->readPixels(info, &pixel, 10, 10));
  EXPECT_EQ(pixel, 0xFFFF0000u) << "off-screen tiles kept stale content after scrolling back";
}

}  // namespace tgfx
