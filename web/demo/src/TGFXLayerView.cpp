/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "TGFXLayerView.h"
#include "drawers/LayerTreeDrawer.h"
#include "tgfx/platform/Print.h"
namespace hello2d {

TGFXLayerView::TGFXLayerView(std::string canvasID, const emscripten::val& nativeImage)
    : TGFXView(std::move(canvasID), nativeImage) {
}

bool TGFXLayerView::onDraw(tgfx::Surface* surface, const drawers::AppHost* appHost, int) {
  auto drawer = drawers::Drawer::GetByName(_treeName + "Drawer");
  if (drawer && drawer->isLayerTreeDrawer()) {
    auto layerTreeDrawer = static_cast<drawers::LayerTreeDrawer*>(drawer);
    return layerTreeDrawer->render(surface, appHost);
  } else {
    tgfx::PrintError("Drawer :%s not found", _treeName.c_str());
  }
  return false;
}

void TGFXLayerView::hitTest(float x, float y) {
  auto drawer = drawers::Drawer::GetByName(_treeName + "Drawer");
  if (drawer && drawer->isLayerTreeDrawer()) {
    auto scale = appHost->density();
    auto layerTreeDrawer = static_cast<drawers::LayerTreeDrawer*>(drawer);
    // convert to layer tree drawer scale
    auto layers = layerTreeDrawer->click(x * scale, y * scale);
    tgfx::PrintLog("hit %f  %f", x, y);
    for (auto& layer : layers) {
      tgfx::PrintLog("layer %p: %s %d ", layer.get(), layer->name().c_str(), layer->type());
    }
  } else {
    tgfx::PrintError("Drawer :%s not found", _treeName.c_str());
  }
}
}  // namespace hello2d
