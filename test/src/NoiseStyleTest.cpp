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

#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/layerstyles/NoiseStyle.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(NoiseStyleTest, NoiseShiftText) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1000, 1000);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(1000);
  back->setHeight(1000);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  auto noise = NoiseStyle::MakeMono(6.0f, 0.5f, Color::FromRGBA(0, 0, 0, 128), 42.0f);

  auto textLayer = TextLayer::Make();
  textLayer->setText("DEF");
  textLayer->setFont(Font(typeface, 100.f));
  textLayer->setTextColor(Color::FromRGBA(192, 192, 192, 255));
  textLayer->setLayerStyles({noise});
  back->addChild(textLayer);
  displayList->root()->addChild(back);

  for (int i = 0; i < 8; i++) {
    surface->getCanvas()->clear();
    textLayer->setMatrix(Matrix::MakeTrans(50.f + static_cast<float>(i) * 20.f, 100.f));
    displayList->render(surface.get());
    EXPECT_TRUE(
        Baseline::Compare(surface, "NoiseStyleTest/NoiseShiftText_frame" + std::to_string(i)));
  }
}

}  // namespace tgfx
