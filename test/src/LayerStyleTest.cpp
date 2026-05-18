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
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/layerstyles/NoiseStyle.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(LayerStyleTest, NoiseStyleMovingRect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 600, 400);
  DisplayList displayList;
  displayList.setRenderMode(RenderMode::Tiled);
  displayList.setTileSize(64);

  auto parent = ShapeLayer::Make();
  parent->setFillStyle(ShapeStyle::Make(Color::White()));
  Path parentPath;
  parentPath.addRect(Rect::MakeXYWH(50, 50, 300, 300));
  parent->setPath(parentPath);
  displayList.root()->addChild(parent);

  auto child = ShapeLayer::Make();
  child->setFillStyle(ShapeStyle::Make(Color::White()));
  auto noise = NoiseStyle::MakeMono(4, 0.5f, Color{0.0f, 0.0f, 0.0f, 0.5f}, 6903);
  child->setLayerStyles({noise});
  parent->addChild(child);

  for (int i = 0; i < 8; i++) {
    Path path;
    auto offsetX = 50.f + static_cast<float>(i) * 20.f;
    path.addRect(Rect::MakeXYWH(offsetX, 100.f, 100.f, 100.f));
    child->setPath(path);

    displayList.render(surface.get());
    EXPECT_TRUE(
        Baseline::Compare(surface, "LayerStyleTest/NoiseStyleMovingRect_" + std::to_string(i)));
  }
}

}  // namespace tgfx
