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
#include <tgfx/layers/ImageLayer.h>
#include <tgfx/layers/ShapeLayer.h>
#include "drawers/Drawer.h"

#pragma once

namespace drawers {

class CustomLayer;

class LayerDemoDrawer : public Drawer {
 public:
  LayerDemoDrawer() : Drawer("LayerDemo") {
    initDisplayList();
  }

  void changeLightAndDarkMode();

  std::vector<std::shared_ptr<tgfx::Layer>> getLayersUnderPoint(float x, float y) const;

 protected:
  bool onDraw(tgfx::Surface* surface, const AppHost* host) override;

 private:
  void initDisplayList();

  void updateRootMatrix(const AppHost* host);

  void updateImage(const AppHost* host);

  void updateFont(const AppHost* host);

  // use to replaceImage
  std::shared_ptr<tgfx::ImageLayer> imageLayer;
  std::shared_ptr<tgfx::ShapeLayer> imageMaskLayer;

  // use to updateFont
  std::shared_ptr<CustomLayer> textLayer;

  // update blendmode
  std::shared_ptr<tgfx::Layer> progressBar;

  // use to updateMatrix
  std::shared_ptr<tgfx::Layer> root;

  tgfx::DisplayList displayList;
};
}  // namespace drawers
