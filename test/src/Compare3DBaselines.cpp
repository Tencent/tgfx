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

// Temporary comparison harness: replays the six 3D baseline scenes under review and saves each
// result to test/out/Compare3D so the PR branch and main can be diffed pixel by pixel without
// touching the baseline mechanism.

#include <cstdio>
#include "core/utils/MathExtra.h"
#include "hello2d/LayerBuilder.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/filters/DropShadowFilter.h"
#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {

static inline Matrix3D MakePerspectiveMatrix() {
  auto perspectiveMatrix = Matrix3D::I();
  constexpr float eyeDistance = 1200.f;
  perspectiveMatrix.setRowColumn(3, 2, -1.f / eyeDistance);
  return perspectiveMatrix;
}

static void SaveStage(const std::shared_ptr<Surface>& surface, const std::string& key) {
  Bitmap bitmap(surface->width(), surface->height(), false, false, surface->colorSpace());
  Pixmap pixmap(bitmap);
  ASSERT_TRUE(surface->readPixels(pixmap.info(), pixmap.writablePixels()));
  auto data = ImageCodec::Encode(pixmap, EncodedFormat::PNG, 100);
  ASSERT_TRUE(data != nullptr);
  SaveFile(data, "Compare3D/" + key + ".png");
}

}  // namespace

TGFX_TEST(Compare3DBaselines, BackgroundBlur3DLayer) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 250, 250);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();

  auto backImage = MakeImage("resources/assets/HappyNewYear.png");
  auto layerA = ImageLayer::Make();
  layerA->setName("layerA");
  layerA->setImage(backImage);
  layerA->setMatrix(Matrix::MakeScale(250.f / 1024.f));
  displayList->root()->addChild(layerA);

  auto layerB1Image = MakeImage("resources/assets/glyph3.png")->makeSubset(Rect::MakeWH(150, 150));
  auto layerB1 = ImageLayer::Make();
  layerB1->setName("layerB1");
  layerB1->setImage(layerB1Image);
  layerB1->setAlpha(0.8f);
  {
    auto size = Size::Make(150, 150);
    auto anchor = Point::Make(0.5f, 0.5f);
    auto offsetToAnchor =
        Matrix3D::MakeTranslate(-anchor.x * size.width, -anchor.y * size.height, 0);
    auto invOffsetToAnchor =
        Matrix3D::MakeTranslate(anchor.x * size.width, anchor.y * size.height, 0);
    auto rotate = Matrix3D::MakeRotate({0, 1, 0}, 25);
    auto perspective = Matrix3D::I();
    perspective.setRowColumn(3, 2, -1.0f / 500.0f);
    auto origin = Matrix3D::MakeTranslate(50, 50, 0);
    layerB1->setMatrix3D(origin * invOffsetToAnchor * perspective * rotate * offsetToAnchor);
  }
  displayList->root()->addChild(layerB1);

  auto layerB2 = SolidLayer::Make();
  layerB2->setName("layerB2");
  layerB2->setColor(Color::Green());
  layerB2->setAlpha(0.3f);
  layerB2->setWidth(60);
  layerB2->setHeight(80);
  {
    auto size = Size::Make(60, 80);
    auto anchor = Point::Make(0.5f, 0.5f);
    auto offsetToAnchor =
        Matrix3D::MakeTranslate(-anchor.x * size.width, -anchor.y * size.height, 0);
    auto invOffsetToAnchor =
        Matrix3D::MakeTranslate(anchor.x * size.width, anchor.y * size.height, 0);
    auto rotate = Matrix3D::MakeRotate({1, 0, 0}, 20);
    auto perspective = Matrix3D::I();
    perspective.setRowColumn(3, 2, -1.0f / 500.0f);
    auto origin = Matrix3D::MakeTranslate(80, 150, 0);
    layerB2->setMatrix3D(origin * invOffsetToAnchor * perspective * rotate * offsetToAnchor);
  }
  layerB2->setLayerStyles({BackgroundBlurStyle::Make(5, 5)});
  displayList->root()->addChild(layerB2);

  auto layerCImage = MakeImage("resources/assets/glyph2.png")->makeSubset(Rect::MakeWH(80, 80));
  auto layerC = ImageLayer::Make();
  layerC->setName("layerC");
  layerC->setImage(layerCImage);
  layerC->setAlpha(0.6f);
  {
    auto size = Size::Make(80, 80);
    auto anchor = Point::Make(0.5f, 0.5f);
    auto offsetToAnchor =
        Matrix3D::MakeTranslate(-anchor.x * size.width, -anchor.y * size.height, 0);
    auto invOffsetToAnchor =
        Matrix3D::MakeTranslate(anchor.x * size.width, anchor.y * size.height, 0);
    auto rotate = Matrix3D::MakeRotate({1, 0, 0}, 20);
    auto perspective = Matrix3D::I();
    perspective.setRowColumn(3, 2, -1.0f / 500.0f);
    auto origin = Matrix3D::MakeTranslate(35, 35, 0);
    layerC->setMatrix3D(origin * invOffsetToAnchor * perspective * rotate * offsetToAnchor);
  }
  layerC->setLayerStyles({BackgroundBlurStyle::Make(5, 5)});
  layerB1->addChild(layerC);

  auto layerD = SolidLayer::Make();
  layerD->setName("layerD");
  layerD->setColor(Color::Red());
  layerD->setAlpha(0.6f);
  layerD->setWidth(80);
  layerD->setHeight(80);
  {
    auto size = Size::Make(80, 80);
    auto anchor = Point::Make(0.5f, 0.5f);
    auto offsetToAnchor =
        Matrix3D::MakeTranslate(-anchor.x * size.width, -anchor.y * size.height, 0);
    auto invOffsetToAnchor =
        Matrix3D::MakeTranslate(anchor.x * size.width, anchor.y * size.height, 0);
    auto rotate = Matrix3D::MakeRotate({1, 0, 0}, 20);
    auto perspective = Matrix3D::I();
    perspective.setRowColumn(3, 2, -1.0f / 500.0f);
    auto origin = Matrix3D::MakeTranslate(40, 40, 0);
    layerD->setMatrix3D(origin * invOffsetToAnchor * perspective * rotate * offsetToAnchor);
  }
  layerD->setLayerStyles({BackgroundBlurStyle::Make(5, 5)});
  layerC->addChild(layerD);

  // Case 1: B1.preserve3D=true with no styles, B1 enters 3D context.
  layerB1->setPreserve3D(true);
  displayList->render(surface.get());
  SaveStage(surface, "BackgroundBlur3DLayer_Preserve3D");

  // Case 2 and 3 only mutate state; the harness captures Preserve3D, NestedOffscreen and
  // Nested3D, which match the six scenes under review.
  layerB1->setPreserve3D(false);
  displayList->render(surface.get());
  layerB1->setPreserve3D(true);
  layerB1->setLayerStyles({BackgroundBlurStyle::Make(5, 5)});
  displayList->render(surface.get());

  // Case 4: Direct Layer::draw() entry on a perspective-transformed inner subtree.
  layerD->setAlpha(0.2f);
  surface->getCanvas()->clear();
  layerC->draw(surface->getCanvas());

  // Case 5: layerC carries a BlurFilter on top of its BackgroundBlur style.
  layerD->setAlpha(0.6f);
  layerB1->setLayerStyles({});
  layerB1->setPreserve3D(true);
  layerC->setFilters({BlurFilter::Make(2, 2)});
  displayList->render(surface.get());
  SaveStage(surface, "BackgroundBlur3DLayer_NestedOffscreen");

  // Case 6: nested 3D rendering context inside a non-preserve3D leaf.
  layerC->setFilters({});
  layerC->removeChildren();

  auto layerE = Layer::Make();
  layerE->setName("layerE");
  layerE->setPreserve3D(true);
  {
    auto size = Size::Make(80, 80);
    auto anchor = Point::Make(0.5f, 0.5f);
    auto offsetToAnchor =
        Matrix3D::MakeTranslate(-anchor.x * size.width, -anchor.y * size.height, 0);
    auto invOffsetToAnchor =
        Matrix3D::MakeTranslate(anchor.x * size.width, anchor.y * size.height, 0);
    auto rotate = Matrix3D::MakeRotate({0, 1, 0}, 20);
    auto perspective = Matrix3D::I();
    perspective.setRowColumn(3, 2, -1.0f / 500.0f);
    auto origin = Matrix3D::MakeTranslate(40, 40, 0);
    layerE->setMatrix3D(origin * invOffsetToAnchor * perspective * rotate * offsetToAnchor);
  }
  layerC->addChild(layerE);

  auto layerF = SolidLayer::Make();
  layerF->setName("layerF");
  layerF->setColor(Color::Blue());
  layerF->setAlpha(0.6f);
  layerF->setWidth(60);
  layerF->setHeight(60);
  {
    auto size = Size::Make(60, 60);
    auto anchor = Point::Make(0.5f, 0.5f);
    auto offsetToAnchor =
        Matrix3D::MakeTranslate(-anchor.x * size.width, -anchor.y * size.height, 0);
    auto invOffsetToAnchor =
        Matrix3D::MakeTranslate(anchor.x * size.width, anchor.y * size.height, 0);
    auto rotate = Matrix3D::MakeRotate({1, 0, 0}, 15);
    auto perspective = Matrix3D::I();
    perspective.setRowColumn(3, 2, -1.0f / 500.0f);
    auto origin = Matrix3D::MakeTranslate(10, 10, 0);
    layerF->setMatrix3D(origin * invOffsetToAnchor * perspective * rotate * offsetToAnchor);
  }
  layerF->setLayerStyles({BackgroundBlurStyle::Make(5, 5)});
  layerE->addChild(layerF);

  displayList->render(surface.get());
  SaveStage(surface, "BackgroundBlur3DLayer_Nested3D");
}

TGFX_TEST(Compare3DBaselines, Layer3DTree) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surfaceWidth = 720;
  auto surfaceHeight = 720;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight, false, 4);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  hello2d::AppHost appHost;
  appHost.addImage("bridge", MakeImage("resources/assets/bridge.jpg"));
  appHost.addImage("TGFX", MakeImage("resources/assets/tgfx.png"));
  appHost.addTypeface("default", MakeTypeface("resources/font/NotoSansSC-Regular.otf"));
  appHost.addTypeface("emoji", MakeTypeface("resources/font/NotoColorEmoji.ttf"));

  DisplayList displayList;
  displayList.setRenderMode(RenderMode::Direct);

  const auto& names = hello2d::LayerBuilder::Names();
  for (size_t i = 0; i < names.size(); ++i) {
    if (names[i] != "Layer3DTree") {
      continue;
    }
    auto builder = hello2d::LayerBuilder::GetByIndex(static_cast<int>(i));
    ASSERT_TRUE(builder != nullptr);
    auto layer = builder->buildLayerTree(&appHost);
    ASSERT_TRUE(layer != nullptr);
    displayList.root()->addChild(layer);
    hello2d::LayerBuilder::ApplyCenteringTransform(layer, static_cast<float>(surfaceWidth),
                                                   static_cast<float>(surfaceHeight));
    canvas->clear();
    hello2d::DrawBackground(canvas, surfaceWidth, surfaceHeight, 2.0f);
    displayList.render(surface.get(), false);
    SaveStage(surface, "Layer3DTree");
  }
  canvas->clear();
}

TGFX_TEST(Compare3DBaselines, Contour3DWithDropShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 600, 600);
  ASSERT_TRUE(surface != nullptr);

  auto displayList = std::make_unique<DisplayList>();
  displayList->setZoomScale(2.0f);

  auto parentLayer = SolidLayer::Make();
  parentLayer->setColor(Color::FromRGBA(200, 200, 200, 100));
  parentLayer->setWidth(150);
  parentLayer->setHeight(150);
  parentLayer->setMatrix(Matrix::MakeTrans(75, 75));
  parentLayer->setPreserve3D(true);

  auto dropShadow = DropShadowStyle::Make(10, 10, 8, 8, Color::FromRGBA(0, 255, 0, 255));
  dropShadow->setShowBehindLayer(false);
  parentLayer->setLayerStyles({dropShadow});

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
    auto origin = Matrix3D::MakeTranslate(-20, -20, 0);
    child1->setMatrix3D(origin * invOffsetToAnchor * perspective * rotate * offsetToAnchor);
  }

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
    auto origin = Matrix3D::MakeTranslate(90, 90, 0);
    child2->setMatrix3D(origin * invOffsetToAnchor * perspective * rotate * offsetToAnchor);
  }

  parentLayer->addChild(child1);
  parentLayer->addChild(child2);
  displayList->root()->addChild(parentLayer);

  displayList->render(surface.get());
  SaveStage(surface, "Contour3DWithDropShadow");
}

TGFX_TEST(Compare3DBaselines, MatrixPreserve3D) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 200);
  ASSERT_TRUE(surface != nullptr);
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
    auto offsetToAnchorMatrix =
        Matrix3D::MakeTranslate(-anchor.x * imageSize.width, -anchor.y * imageSize.height, 0.f);
    auto invOffsetToAnchorMatrix =
        Matrix3D::MakeTranslate(anchor.x * imageSize.width, anchor.y * imageSize.height, 0.f);
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
    auto origin = Point::Make(125, 105);
    auto originTranslateMatrix = Matrix3D::MakeTranslate(origin.x, origin.y, 0.f);
    imageMatrix3D = originTranslateMatrix * invOffsetToAnchorMatrix * perspectiveMatrix *
                    modelMatrix * offsetToAnchorMatrix;
  }
  contentLayer->addChild(imageLayer);
  imageLayer->setMatrix3D(imageMatrix3D);
  displayList->render(surface.get());

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

  imageBlurLayer->removeFromParent();
  imageLayer->setMatrix3D(imageMatrix3D);
  auto rect = Rect::MakeXYWH(50, 50, 200, 100);
  Path path = {};
  path.addRoundRect(rect, 20, 20);
  auto shaperLayer = ShapeLayer::Make();
  shaperLayer->setPath(path);
  shaperLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(0, 0, 255, 128)));
  shaperLayer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  {
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

  contentLayer->setPreserve3D(true);
  displayList->render(surface.get());
  SaveStage(surface, "Matrix_3D_2D_3D_Preserve3D");
}

}  // namespace tgfx
