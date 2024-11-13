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
    "先帝创业\t未半而中道崩殂，今天下三分，益州疲弊，此诚危急存亡之秋也。"
    "然侍卫之臣不懈于内，忠志之士忘身于外者，盖追先帝之殊遇，欲报之于陛下也。"
    "诚宜开张圣听，以光先帝遗德，恢弘志士之气，不宜妄自菲薄，引喻失义，以塞忠谏之路也。"
    "宫中府中，俱为一体；陟罚臧否，不宜异同。"
    "若有作奸犯科及为忠善者，宜付有司论其刑赏，以昭陛下平明之理，不宜偏私，使内外异法也。hello tfgx!";

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
  EXPECT_TRUE(Baseline::Compare(surface, "TextAlignTest/TextAlign"));
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
  textLayer->setWidth(10);
  textLayer->setHeight(10);
  textLayer->setAutoWrap(false);
  textLayer->setTextAlign(TextAlign::Left);
  textLayer->setFont(font);
  parentLayer->addChild(textLayer);

  // auto textLayer2 = TextLayer::Make();
  // textLayer2->setMatrix(Matrix::MakeTrans(0.0f, 150.0f));
  // textLayer2->setName("text_layer2");
  // textLayer2->setTextColor(Color::White());
  // textLayer2->setText(text);
  // textLayer2->setWidth(0);
  // textLayer2->setHeight(0);
  // textLayer2->setAutoWrap(true);
  // textLayer2->setTextAlign(TextAlign::Left);
  // textLayer2->setFont(font);
  // parentLayer->addChild(textLayer2);
  //
  // auto textLayer3 = TextLayer::Make();
  // textLayer3->setMatrix(Matrix::MakeTrans(0.0f, 300.0f));
  // textLayer3->setName("text_layer3");
  // textLayer3->setTextColor(Color::White());
  // textLayer3->setText(text);
  // textLayer3->setWidth(0);
  // textLayer3->setHeight(0);
  // textLayer3->setAutoWrap(true);
  // textLayer3->setTextAlign(TextAlign::Center);
  // textLayer3->setFont(font);
  // parentLayer->addChild(textLayer3);
  //
  // auto textLayer4 = TextLayer::Make();
  // textLayer4->setMatrix(Matrix::MakeTrans(0.0f, 450.0f));
  // textLayer4->setName("text_layer4");
  // textLayer4->setTextColor(Color::White());
  // textLayer4->setText(text);
  // textLayer4->setWidth(0);
  // textLayer4->setHeight(0);
  // textLayer4->setAutoWrap(true);
  // textLayer4->setTextAlign(TextAlign::Right);
  // textLayer4->setFont(font);
  // parentLayer->addChild(textLayer4);
  //
  // auto textLayer5 = TextLayer::Make();
  // textLayer5->setMatrix(Matrix::MakeTrans(0.0f, 600.0f));
  // textLayer5->setName("text_layer5");
  // textLayer5->setTextColor(Color::White());
  // textLayer5->setText(text);
  // textLayer5->setWidth(0);
  // textLayer5->setHeight(0);
  // textLayer5->setAutoWrap(true);
  // textLayer5->setTextAlign(TextAlign::Justify);
  // textLayer5->setFont(font);
  // parentLayer->addChild(textLayer5);

  displayList->root()->addChild(rootLayer);
  displayList->render(surface.get());

  context->submit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextAlignTest/TextAlignWidth10Height10"));
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
