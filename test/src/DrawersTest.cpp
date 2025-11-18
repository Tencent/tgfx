/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "../../include/tgfx/core/Log.h"
#include "drawers/Drawer.h"
#include "utils/TestUtils.h"

namespace tgfx {
TGFX_TEST(DrawersTest, Compare) {
  drawers::AppHost appHost(720, 720, 2.0f);
  appHost.addImage("bridge", MakeImage("resources/assets/bridge.jpg"));
  appHost.addTypeface("default", MakeTypeface("resources/font/NotoSansSC-Regular.otf"));
  appHost.addTypeface("emoji", MakeTypeface("resources/font/NotoColorEmoji.ttf"));

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, appHost.width(), appHost.height(), false, 4);
  auto canvas = surface->getCanvas();
  auto drawerNames = drawers::Drawer::Names();
  for (auto& name : drawerNames) {
    auto drawer = drawers::Drawer::GetByName(name);
    ASSERT_TRUE(drawer != nullptr);
    canvas->clear();
    drawer->draw(canvas, &appHost);
    auto key = "DrawersTest/" + name;
    auto result = Baseline::Compare(surface, key);
    if (!result) {
      ADD_FAILURE();
      LOGI("Baseline::Compare failed for %s", key.c_str());
    }
  }
}
}  // namespace tgfx
