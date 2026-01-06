/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "core/utils/Log.h"
#include "hello2d/LayerBuilder.h"
#include "tgfx/layers/DisplayList.h"
#include "utils/TestUtils.h"

namespace tgfx {
TGFX_TEST(Hello2DTest, Compare) {
  hello2d::AppHost appHost;
  appHost.addImage("bridge", MakeImage("resources/assets/bridge.jpg"));
  appHost.addImage("TGFX", MakeImage("resources/assets/tgfx.png"));
  appHost.addTypeface("default", MakeTypeface("resources/font/NotoSansSC-Regular.otf"));
  appHost.addTypeface("emoji", MakeTypeface("resources/font/NotoColorEmoji.ttf"));

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surfaceWidth = 720;
  auto surfaceHeight = 720;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight, false, 4);
  auto canvas = surface->getCanvas();

  DisplayList displayList;
  displayList.setRenderMode(RenderMode::Direct);

  auto& builderNames = hello2d::LayerBuilder::Names();
  for (size_t i = 0; i < builderNames.size(); ++i) {
    const auto& name = builderNames[i];

    // Build layer and add to DisplayList
    auto builder = hello2d::LayerBuilder::GetByIndex(static_cast<int>(i));
    if (builder) {
      auto layer = builder->buildLayerTree(&appHost);
      if (layer) {
        displayList.root()->removeChildren();
        displayList.root()->addChild(layer);
        hello2d::LayerBuilder::ApplyCenteringTransform(layer, static_cast<float>(surfaceWidth),
                                                       static_cast<float>(surfaceHeight));
      }
    }

    // Draw background and render DisplayList
    canvas->clear();
    hello2d::DrawBackground(canvas, surfaceWidth, surfaceHeight, 2.0f);
    displayList.render(surface.get(), false);

    auto key = "Hello2DTest/" + name;
    auto result = Baseline::Compare(surface, key);
    if (!result) {
      ADD_FAILURE() << "Baseline comparison failed for key: " << key;
    }
  }
  canvas->clear();
}
}  // namespace tgfx
