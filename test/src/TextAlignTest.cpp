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

#include <tgfx/layers/DisplayList.h>
#include <tgfx/layers/Layer.h>
#include <tgfx/layers/TextLayer.h>
#include "utils/TestUtils.h"

namespace tgfx {
const std::string text =
    "abcdefghijklmnopqrstuvwxyz\njpyq\r这是一段测试文字\r\n"
    "先帝创业未半而中道崩殂，今天下三分，益州疲弊，此诚危急存亡之秋也。"
    "然侍卫之臣不懈于内，忠志之士忘身于外者，盖追先帝之殊遇，欲报之于陛下也。"
    "诚宜开张圣听，以光先帝遗德，恢弘志士之气，不宜妄自菲薄，引喻失义，以塞忠谏之路也。"
    "宫中府中，俱为一体；陟罚臧否，不宜异同。"
    "若有作奸犯科及为忠善者，宜付有司论其刑赏，以昭陛下平明之理，不宜偏私，使内外异法也。hello\ttfgx!";

TGFX_TEST(TextAlignTest, TextAlign) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto surface = Surface::Make(context, 1000, 800);
  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();
  rootLayer->setName("root_layer");

  auto parentLayer = Layer::Make();
  parentLayer->setName("parent_layer");
  rootLayer->addChild(parentLayer);

  auto textLayer = TextLayer::Make();
  textLayer->setName("text_layer");
  textLayer->setTextColor(Color::White());
  textLayer->setText(text);
  textLayer->setWidth(200);
  textLayer->setHeight(0);
  textLayer->setAutoWrap(true);
  textLayer->setTextAlign(TextAlign::Left);
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  Font font(typeface, 20);
  textLayer->setFont(font);
  parentLayer->addChild(textLayer);

  auto textLayer2 = TextLayer::Make();
  textLayer2->setMatrix(Matrix::MakeTrans(250.0f, 0.0f));
  textLayer2->setName("text_layer2");
  textLayer2->setTextColor(Color::White());
  textLayer2->setText(text);
  textLayer2->setWidth(200);
  textLayer2->setHeight(0);
  textLayer2->setAutoWrap(true);
  textLayer2->setTextAlign(TextAlign::Center);
  textLayer2->setFont(font);
  parentLayer->addChild(textLayer2);

  auto textLayer3 = TextLayer::Make();
  textLayer3->setMatrix(Matrix::MakeTrans(500.0f, 0.0f));
  textLayer3->setName("text_layer3");
  textLayer3->setTextColor(Color::White());
  textLayer3->setText(text);
  textLayer3->setWidth(200);
  textLayer3->setHeight(0);
  textLayer3->setAutoWrap(true);
  textLayer3->setTextAlign(TextAlign::Right);
  textLayer3->setFont(font);
  parentLayer->addChild(textLayer3);

  auto textLayer4 = TextLayer::Make();
  textLayer4->setMatrix(Matrix::MakeTrans(750.0f, 0.0f));
  textLayer4->setName("text_layer4");
  textLayer4->setTextColor(Color::White());
  textLayer4->setText(text);
  textLayer4->setWidth(200);
  textLayer4->setHeight(0);
  textLayer4->setAutoWrap(true);
  textLayer4->setTextAlign(TextAlign::Justify);
  textLayer4->setFont(font);
  parentLayer->addChild(textLayer4);

  displayList->root()->addChild(rootLayer);
  displayList->render(surface.get());

  context->submit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextAlignTest/TextAlign1"));
  device->unlock();
}

TGFX_TEST(TextAlignTest, TextAlignWidth0Height0) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto surface = Surface::Make(context, 800, 800);
  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();
  rootLayer->setName("root_layer");

  auto parentLayer = Layer::Make();
  parentLayer->setName("parent_layer");
  rootLayer->addChild(parentLayer);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  Font font(typeface, 20);

  auto textLayer = TextLayer::Make();
  textLayer->setName("text_layer1");
  textLayer->setTextColor(Color::White());
  textLayer->setText(text);
  textLayer->setWidth(0);
  textLayer->setHeight(0);
  textLayer->setAutoWrap(false);
  textLayer->setTextAlign(TextAlign::Justify);
  textLayer->setFont(font);
  parentLayer->addChild(textLayer);

  auto textLayer2 = TextLayer::Make();
  textLayer2->setMatrix(Matrix::MakeTrans(0.0f, 150.0f));
  textLayer2->setName("text_layer2");
  textLayer2->setTextColor(Color::White());
  textLayer2->setText(text);
  textLayer2->setWidth(0);
  textLayer2->setHeight(0);
  textLayer2->setAutoWrap(true);
  textLayer2->setTextAlign(TextAlign::Left);
  textLayer2->setFont(font);
  parentLayer->addChild(textLayer2);

  auto textLayer3 = TextLayer::Make();
  textLayer3->setMatrix(Matrix::MakeTrans(0.0f, 300.0f));
  textLayer3->setName("text_layer3");
  textLayer3->setTextColor(Color::White());
  textLayer3->setText(text);
  textLayer3->setWidth(0);
  textLayer3->setHeight(0);
  textLayer3->setAutoWrap(true);
  textLayer3->setTextAlign(TextAlign::Center);
  textLayer3->setFont(font);
  parentLayer->addChild(textLayer3);

  auto textLayer4 = TextLayer::Make();
  textLayer4->setMatrix(Matrix::MakeTrans(0.0f, 450.0f));
  textLayer4->setName("text_layer4");
  textLayer4->setTextColor(Color::White());
  textLayer4->setText(text);
  textLayer4->setWidth(0);
  textLayer4->setHeight(0);
  textLayer4->setAutoWrap(true);
  textLayer4->setTextAlign(TextAlign::Right);
  textLayer4->setFont(font);
  parentLayer->addChild(textLayer4);

  auto textLayer5 = TextLayer::Make();
  textLayer5->setMatrix(Matrix::MakeTrans(0.0f, 600.0f));
  textLayer5->setName("text_layer5");
  textLayer5->setTextColor(Color::White());
  textLayer5->setText(text);
  textLayer5->setWidth(0);
  textLayer5->setHeight(0);
  textLayer5->setAutoWrap(true);
  textLayer5->setTextAlign(TextAlign::Justify);
  textLayer5->setFont(font);
  parentLayer->addChild(textLayer5);

  displayList->root()->addChild(rootLayer);
  displayList->render(surface.get());

  context->submit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextAlignTest/TextAlignWidth0Height0"));
  device->unlock();
}

TGFX_TEST(TextAlignTest, TextAlignWidth10Height10) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto surface = Surface::Make(context, 500, 300);
  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();
  rootLayer->setName("root_layer");

  auto parentLayer = Layer::Make();
  parentLayer->setName("parent_layer");
  rootLayer->addChild(parentLayer);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  Font font(typeface, 20);

  auto textLayer = TextLayer::Make();
  textLayer->setName("text_layer1");
  textLayer->setTextColor(Color::White());
  textLayer->setText(text);
  textLayer->setWidth(10);
  textLayer->setHeight(10);
  textLayer->setAutoWrap(false);
  textLayer->setTextAlign(TextAlign::Left);
  textLayer->setFont(font);
  parentLayer->addChild(textLayer);

  auto textLayer2 = TextLayer::Make();
  textLayer2->setMatrix(Matrix::MakeTrans(0.0f, 50.0f));
  textLayer2->setName("text_layer2");
  textLayer2->setTextColor(Color::White());
  textLayer2->setText(text);
  textLayer2->setWidth(10);
  textLayer2->setHeight(10);
  textLayer2->setAutoWrap(true);
  textLayer2->setTextAlign(TextAlign::Left);
  textLayer2->setFont(font);
  parentLayer->addChild(textLayer2);

  auto textLayer3 = TextLayer::Make();
  textLayer3->setMatrix(Matrix::MakeTrans(0.0f, 100.0f));
  textLayer3->setName("text_layer3");
  textLayer3->setTextColor(Color::White());
  textLayer3->setText(text);
  textLayer3->setWidth(10);
  textLayer3->setHeight(10);
  textLayer3->setAutoWrap(true);
  textLayer3->setTextAlign(TextAlign::Center);
  textLayer3->setFont(font);
  parentLayer->addChild(textLayer3);

  auto textLayer4 = TextLayer::Make();
  textLayer4->setMatrix(Matrix::MakeTrans(0.0f, 150.0f));
  textLayer4->setName("text_layer4");
  textLayer4->setTextColor(Color::White());
  textLayer4->setText(text);
  textLayer4->setWidth(10);
  textLayer4->setHeight(10);
  textLayer4->setAutoWrap(true);
  textLayer4->setTextAlign(TextAlign::Right);
  textLayer4->setFont(font);
  parentLayer->addChild(textLayer4);

  auto textLayer5 = TextLayer::Make();
  textLayer5->setMatrix(Matrix::MakeTrans(0.0f, 200.0f));
  textLayer5->setName("text_layer5");
  textLayer5->setTextColor(Color::White());
  textLayer5->setText(text);
  textLayer5->setWidth(10);
  textLayer5->setHeight(10);
  textLayer5->setAutoWrap(true);
  textLayer5->setTextAlign(TextAlign::Justify);
  textLayer5->setFont(font);
  parentLayer->addChild(textLayer5);

  displayList->root()->addChild(rootLayer);
  displayList->render(surface.get());

  context->submit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextAlignTest/TextAlignWidth10Height10"));
  device->unlock();
}

TGFX_TEST(TextAlignTest, VerticalTextAlign) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto surface = Surface::Make(context, 600, 800);
  auto canvas = surface->getCanvas();
  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();
  rootLayer->setName("root_layer");

  auto parentLayer = Layer::Make();
  parentLayer->setName("parent_layer");
  rootLayer->addChild(parentLayer);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  Font font(typeface, 20);
  std::string testText = "这是一段测试文字，TGFX";
  auto textLayer = TextLayer::Make();
  textLayer->setName("text_layer1");
  textLayer->setTextColor(Color::White());
  textLayer->setText(testText);
  textLayer->setWidth(50);
  textLayer->setHeight(400);
  textLayer->setAutoWrap(true);
  textLayer->setTextAlign(TextAlign::Left);
  textLayer->setFont(font);
  parentLayer->addChild(textLayer);
  auto textLayerBounds = textLayer->getBounds();
  textLayer->getGlobalMatrix().mapRect(&textLayerBounds);

  auto textLayer2 = TextLayer::Make();
  textLayer2->setMatrix(Matrix::MakeTrans(100.0f, 0.0f));
  textLayer2->setName("text_layer2");
  textLayer2->setTextColor(Color::White());
  textLayer2->setText(testText);
  textLayer2->setWidth(50);
  textLayer2->setHeight(400);
  textLayer2->setAutoWrap(true);
  textLayer2->setTextAlign(TextAlign::Center);
  textLayer2->setFont(font);
  parentLayer->addChild(textLayer2);
  auto textLayer2Bounds = textLayer2->getBounds();
  textLayer2->getGlobalMatrix().mapRect(&textLayer2Bounds);

  auto textLayer3 = TextLayer::Make();
  textLayer3->setMatrix(Matrix::MakeTrans(200.0f, 0.0f));
  textLayer3->setName("text_layer3");
  textLayer3->setTextColor(Color::White());
  textLayer3->setText(testText);
  textLayer3->setWidth(50);
  textLayer3->setHeight(400);
  textLayer3->setAutoWrap(true);
  textLayer3->setTextAlign(TextAlign::Right);
  textLayer3->setFont(font);
  parentLayer->addChild(textLayer3);
  auto textLayer3Bounds = textLayer3->getBounds();
  textLayer3->getGlobalMatrix().mapRect(&textLayer3Bounds);

  auto textLayer4 = TextLayer::Make();
  textLayer4->setMatrix(Matrix::MakeTrans(300.0f, 0.0f));
  textLayer4->setName("text_layer4");
  textLayer4->setTextColor(Color::White());
  textLayer4->setText(testText);
  textLayer4->setWidth(50);
  textLayer4->setHeight(400);
  textLayer4->setAutoWrap(true);
  textLayer4->setTextAlign(TextAlign::Justify);
  textLayer4->setFont(font);
  parentLayer->addChild(textLayer4);
  auto textLayer4Bounds = textLayer4->getBounds();
  textLayer4->getGlobalMatrix().mapRect(&textLayer4Bounds);

  auto textLayer5 = TextLayer::Make();
  textLayer5->setMatrix(Matrix::MakeTrans(0.0f, 300.0f));
  textLayer5->setName("text_layer5");
  textLayer5->setTextColor(Color::White());
  textLayer5->setText(testText);
  textLayer5->setWidth(1);
  textLayer5->setHeight(400);
  textLayer5->setAutoWrap(true);
  textLayer5->setTextAlign(TextAlign::Left);
  textLayer5->setFont(font);
  parentLayer->addChild(textLayer5);
  auto textLayer5Bounds = textLayer5->getBounds();
  textLayer5->getGlobalMatrix().mapRect(&textLayer5Bounds);

  auto textLayer6 = TextLayer::Make();
  textLayer6->setMatrix(Matrix::MakeTrans(100.0f, 300.0f));
  textLayer6->setName("text_layer6");
  textLayer6->setTextColor(Color::White());
  textLayer6->setText(testText);
  textLayer6->setWidth(1);
  textLayer6->setHeight(400);
  textLayer6->setAutoWrap(true);
  textLayer6->setTextAlign(TextAlign::Center);
  textLayer6->setFont(font);
  parentLayer->addChild(textLayer6);
  auto textLayer6Bounds = textLayer6->getBounds();
  textLayer6->getGlobalMatrix().mapRect(&textLayer6Bounds);

  auto textLayer7 = TextLayer::Make();
  textLayer7->setMatrix(Matrix::MakeTrans(200.0f, 300.0f));
  textLayer7->setName("text_layer7");
  textLayer7->setTextColor(Color::White());
  textLayer7->setText(testText);
  textLayer7->setWidth(1);
  textLayer7->setHeight(400);
  textLayer7->setAutoWrap(true);
  textLayer7->setTextAlign(TextAlign::Right);
  textLayer7->setFont(font);
  parentLayer->addChild(textLayer7);
  auto textLayer7Bounds = textLayer7->getBounds();
  textLayer7->getGlobalMatrix().mapRect(&textLayer7Bounds);

  auto textLayer8 = TextLayer::Make();
  textLayer8->setMatrix(Matrix::MakeTrans(300.0f, 300.0f));
  textLayer8->setName("text_layer8");
  textLayer8->setTextColor(Color::White());
  textLayer8->setText(testText);
  textLayer8->setWidth(1);
  textLayer8->setHeight(400);
  textLayer8->setAutoWrap(true);
  textLayer8->setTextAlign(TextAlign::Justify);
  textLayer8->setFont(font);
  parentLayer->addChild(textLayer8);
  auto textLayer8Bounds = textLayer8->getBounds();
  textLayer8->getGlobalMatrix().mapRect(&textLayer8Bounds);

  displayList->root()->addChild(rootLayer);
  displayList->render(surface.get());

  auto paint = Paint();
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(1.0f);
  paint.setColor(Color::Red());
  canvas->drawRect(textLayerBounds, paint);
  canvas->drawRect(textLayer2Bounds, paint);
  canvas->drawRect(textLayer3Bounds, paint);
  canvas->drawRect(textLayer4Bounds, paint);
  canvas->drawRect(textLayer5Bounds, paint);
  canvas->drawRect(textLayer6Bounds, paint);
  canvas->drawRect(textLayer7Bounds, paint);
  canvas->drawRect(textLayer8Bounds, paint);

  context->submit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextAlignTest/VerticalTextAlign"));
  device->unlock();
}

TGFX_TEST(TextAlignTest, SingleLineTextAlign) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto surface = Surface::Make(context, 600, 400);
  auto canvas = surface->getCanvas();
  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();
  rootLayer->setName("root_layer");

  auto parentLayer = Layer::Make();
  parentLayer->setName("parent_layer");
  rootLayer->addChild(parentLayer);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  Font font(typeface, 20);
  std::string testText = "这是一段测试文字，TGFX";

  auto textLayer = TextLayer::Make();
  textLayer->setName("text_layer1");
  textLayer->setTextColor(Color::White());
  textLayer->setText(testText);
  textLayer->setWidth(500);
  textLayer->setHeight(0);
  textLayer->setAutoWrap(false);
  textLayer->setTextAlign(TextAlign::Left);
  textLayer->setFont(font);
  parentLayer->addChild(textLayer);
  auto textLayerBounds = textLayer->getBounds();
  textLayer->getGlobalMatrix().mapRect(&textLayerBounds);

  auto textLayer2 = TextLayer::Make();
  textLayer2->setMatrix(Matrix::MakeTrans(0.0f, 50.0f));
  textLayer2->setName("text_layer2");
  textLayer2->setTextColor(Color::White());
  textLayer2->setText(testText);
  textLayer2->setWidth(500);
  textLayer2->setHeight(0);
  textLayer2->setAutoWrap(false);
  textLayer2->setTextAlign(TextAlign::Center);
  textLayer2->setFont(font);
  parentLayer->addChild(textLayer2);
  auto textLayer2Bounds = textLayer2->getBounds();
  textLayer2->getGlobalMatrix().mapRect(&textLayer2Bounds);

  auto textLayer3 = TextLayer::Make();
  textLayer3->setMatrix(Matrix::MakeTrans(0.0f, 100.0f));
  textLayer3->setName("text_layer3");
  textLayer3->setTextColor(Color::White());
  textLayer3->setText(testText);
  textLayer3->setWidth(500);
  textLayer3->setHeight(0);
  textLayer3->setAutoWrap(false);
  textLayer3->setTextAlign(TextAlign::Right);
  textLayer3->setFont(font);
  parentLayer->addChild(textLayer3);
  auto textLayer3Bounds = textLayer3->getBounds();
  textLayer3->getGlobalMatrix().mapRect(&textLayer3Bounds);

  auto textLayer4 = TextLayer::Make();
  textLayer4->setMatrix(Matrix::MakeTrans(0.0f, 150.0f));
  textLayer4->setName("text_layer4");
  textLayer4->setTextColor(Color::White());
  textLayer4->setText(testText);
  textLayer4->setWidth(500);
  textLayer4->setHeight(0);
  textLayer4->setAutoWrap(false);
  textLayer4->setTextAlign(TextAlign::Justify);
  textLayer4->setFont(font);
  parentLayer->addChild(textLayer4);
  auto textLayer4Bounds = textLayer4->getBounds();
  textLayer4->getGlobalMatrix().mapRect(&textLayer4Bounds);

  displayList->root()->addChild(rootLayer);
  displayList->render(surface.get());

  auto paint = Paint();
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(1.0f);
  paint.setColor(Color::Red());
  canvas->drawRect(textLayerBounds, paint);
  canvas->drawRect(textLayer2Bounds, paint);
  canvas->drawRect(textLayer3Bounds, paint);
  canvas->drawRect(textLayer4Bounds, paint);

  context->submit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextAlignTest/SingleLineTextAlign"));
  device->unlock();
}

TGFX_TEST(TextAlignTest, TextAlignDebug) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto surface = Surface::Make(context, 800, 800);
  //auto canvas = surface->getCanvas();
  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();
  rootLayer->setName("root_layer");

  auto parentLayer = Layer::Make();
  parentLayer->setName("parent_layer");
  rootLayer->addChild(parentLayer);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  Font font(typeface, 20);
  //std::string testText = "abc";

  auto textLayer = TextLayer::Make();
  textLayer->setName("text_layer1");
  textLayer->setTextColor(Color::White());
  textLayer->setText(text);
  textLayer->setWidth(200);
  textLayer->setHeight(500);
  textLayer->setAutoWrap(true);
  textLayer->setTextAlign(TextAlign::Justify);
  textLayer->setFont(font);
  parentLayer->addChild(textLayer);

  displayList->root()->addChild(rootLayer);
  displayList->render(surface.get());

  context->submit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextAlignTest/TextAlignDebug"));
  device->unlock();
}

TGFX_TEST(TextAlignTest, TextAlignPrint) {
  std::string text = "ab\rcd\nef\r\ngh\n\rij\tk\r\r\rp";
  auto textLayer = TextLayer::Make();
  textLayer->setText(text);
  text = textLayer->preprocessNewLines(text);
  EXPECT_EQ(text, "ab\ncd\nef\ngh\n\nij\tk\n\n\np");
}
}  // namespace tgfx
