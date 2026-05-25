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
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/filters/NoiseFilter.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(PHNoiseTest, MonoAndDuo) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 600, 300);
  ASSERT_TRUE(surface != nullptr);

  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(600);
  back->setHeight(300);

  // Left: Mono noise, density 0.5
  auto monoNoise = NoiseFilter::MakeMono(6.0f, 0.5f, Color::Black(), 42.0f, BlendMode::Multiply);
  auto monoShape = ShapeLayer::Make();
  Path monoPath;
  monoPath.addRect(Rect::MakeWH(300, 300));
  monoShape->setPath(monoPath);
  monoShape->setFillStyle(ShapeStyle::Make(Color::White()));
  monoShape->setFilters({monoNoise});

  // Right: Duo noise, red + blue, density 0.5
  auto duoNoise = NoiseFilter::MakeDuo(6.0f, 0.5f, Color::Red(), Color::Blue(), 42.0f);
  auto duoShape = ShapeLayer::Make();
  Path duoPath;
  duoPath.addRect(Rect::MakeXYWH(300, 0, 300, 300));
  duoShape->setPath(duoPath);
  duoShape->setFillStyle(ShapeStyle::Make(Color::White()));
  duoShape->setFilters({duoNoise});

  back->addChild(monoShape);
  back->addChild(duoShape);
  displayList->root()->addChild(back);

  surface->getCanvas()->clear();
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "PHNoiseTest/MonoAndDuo"));
}

}  // namespace tgfx
