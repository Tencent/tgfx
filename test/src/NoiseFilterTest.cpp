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
#include "tgfx/layers/filters/NoiseFilter.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(NoiseFilterTest, NoiseFilterMovingText) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 500, 500);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(500);
  back->setHeight(500);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  auto noise =
      NoiseFilter::MakeMono(10.0f, 0.5f, Color::FromRGBA(0, 0, 0, 128), 42.0f, BlendMode::SrcOver);

  auto textLayer = TextLayer::Make();
  textLayer->setText("filter");
  textLayer->setFont(Font(typeface, 70.f));
  textLayer->setTextColor(Color::FromRGBA(192, 192, 192, 255));
  textLayer->setFilters({noise});
  back->addChild(textLayer);
  displayList->root()->addChild(back);

  for (int i = 0; i < 8; i++) {
    surface->getCanvas()->clear();
    textLayer->setMatrix(Matrix::MakeTrans(50.f + static_cast<float>(i) * 20.f, 50.f));
    displayList->render(surface.get());
    EXPECT_TRUE(Baseline::Compare(
        surface, "NoiseFilterTest/NoiseFilterMovingText_frame" + std::to_string(i)));
  }
}

}  // namespace tgfx
