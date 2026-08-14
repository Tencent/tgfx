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
#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "utils/TestUtils.h"

namespace tgfx {

// A translucent layer carrying both BackgroundBlurStyle and a zero-blur DropShadowStyle, stacked
// right on top of an opaque layer of the same size.
TGFX_TEST(LayerStyleTest, BackgroundBlurWithSolidDropShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 300);

  auto displayList = std::make_unique<DisplayList>();
  auto background = SolidLayer::Make();
  background->setColor(Color::White());
  background->setWidth(400);
  background->setHeight(300);
  displayList->root()->addChild(background);

  auto layer1 = SolidLayer::Make();
  layer1->setColor(Color::Red());
  layer1->setWidth(200);
  layer1->setHeight(100);
  layer1->setMatrix(Matrix::MakeTrans(100, 100));
  displayList->root()->addChild(layer1);

  auto layer2 = SolidLayer::Make();
  layer2->setColor(Color{1.0f, 0.0f, 0.0f, 0.7f});
  layer2->setWidth(200);
  layer2->setHeight(100);
  layer2->setMatrix(Matrix::MakeTrans(100, 100));
  auto backgroundBlur = BackgroundBlurStyle::Make(100, 100);
  auto dropShadow = DropShadowStyle::Make(20, 20, 0, 0, Color::Black(), false);
  layer2->setLayerStyles({backgroundBlur, dropShadow});
  layer2->setLayerStyles({backgroundBlur});
  displayList->root()->addChild(layer2);

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerStyleTest/BackgroundBlurWithSolidDropShadow"));
}

}  // namespace tgfx
