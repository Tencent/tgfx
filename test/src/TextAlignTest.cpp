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
TGFX_TEST(TextAlignTest, TextAlign) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  auto surface = Surface::Make(context, 800, 800);
  auto canvas = surface->getCanvas();
  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();
  rootLayer->setName("root_layer");

  auto parentLayer = Layer::Make();
  parentLayer->setName("parent_layer");
  rootLayer->addChild(parentLayer);

  auto textLayer = TextLayer::Make();
  textLayer->setName("text_layer");
  textLayer->setTextColor(Color::White());
  textLayer->setText(
      "ajkpy这是一段用于测试文字渲染的内容\nb\rc\r\nd\n\refghijklmnopqrstuvwxyz~!@#$%^&*()_+`"
      "gjpyh\n先帝\t创业未半而中道崩殂，今天下三分，益州疲弊，此诚危急存亡之秋也。"
      "然侍卫之臣不懈于内，忠志之士忘身于外者，盖追先帝之殊遇，欲报之于陛下也。"
      "诚宜开张圣听，以光先帝遗德，恢弘志士之气，不宜妄自菲薄，引喻失义，以塞忠谏之路也。"
      "宫中府中，俱为一体；陟罚臧否，不宜异同。"
      "若有作奸犯科及为忠善者，宜付有司论其刑赏，以昭陛下平明之理，不宜偏私，使内外异法也。");
  //textLayer->setText("abc这是\nf\rg\r\nh\r\rj\n\rk\nppp\tqqq");
  //textLayer->setText("a\n\nbcdefghijklmnopqrstuvwxyz");
  textLayer->setWidth(250);
  textLayer->setHeight(400);
  textLayer->setAutoWrap(true);
  textLayer->setTextAlign(TextAlign::Left);
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  Font font(typeface, 20);
  textLayer->setFont(font);
  parentLayer->addChild(textLayer);
  auto textLayerBounds = textLayer->getBounds();
  textLayer->getGlobalMatrix().mapRect(&textLayerBounds);
  printf("textLayerBounds : %f, %f, %f, %f\n", textLayerBounds.left, textLayerBounds.top,
         textLayerBounds.right, textLayerBounds.bottom);
  EXPECT_EQ(textLayerBounds, Rect::MakeLTRB(0.0f, 10.959999f, 249.0f, 668.080017f));

  displayList->root()->addChild(rootLayer);
  displayList->render(surface.get());

  auto paint = Paint();
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(1.0f);
  paint.setColor(Color::Red());
  canvas->drawRect(textLayerBounds, paint);

  context->submit();
  EXPECT_TRUE(Baseline::Compare(surface, "TextAlignTest/TextAlign"));
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
