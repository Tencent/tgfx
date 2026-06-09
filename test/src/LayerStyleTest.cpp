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

#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/layerstyles/NoiseStyle.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(LayerStyleTest, NoiseDensitySweep) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto layer = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeWH(100.f, 100.f));
  layer->setPath(path);
  layer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(60, 120, 200)));

  std::vector<std::shared_ptr<LayerStyle>> styles;
  for (int i = 0; i < 10; ++i) {
    auto density = static_cast<float>(i + 1) * 0.1f;
    styles.push_back(NoiseStyle::MakeMono(8.0f, density, Color::FromRGBA(255, 0, 0, 128),
                                          42.0f + static_cast<float>(i)));
  }
  layer->setLayerStyles(std::move(styles));

  auto bounds = layer->getBounds(nullptr, true);

  auto surface = Surface::Make(context, static_cast<int>(bounds.width() + 100.f),
                               static_cast<int>(bounds.height() + 100.f));
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear();
  surface->getCanvas()->translate(-bounds.left + 50.f, -bounds.top + 50.f);
  layer->draw(surface->getCanvas());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerStyleTest/NoiseDensitySweep"));
}

}  // namespace tgfx
