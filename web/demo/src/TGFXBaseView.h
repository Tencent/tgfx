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

#pragma once

#include <emscripten/bind.h>
#include "drawers/Drawer.h"
#include "tgfx/gpu/opengl/webgl/WebGLWindow.h"

namespace hello2d {

struct Item {
  float x;
  float y;
  float width;
  float speed;
};

class TGFXBaseView {
 public:
  TGFXBaseView(const std::string& canvasID);

  void setDrawItemsCount(size_t count);

  void setImagePath(const std::string& imagePath);

  void updateSize(float devicePixelRatio);

  void draw(int drawIndex);

 protected:
  std::shared_ptr<drawers::AppHost> appHost;

 private:
  std::string canvasID = "";
  std::shared_ptr<tgfx::Window> window = nullptr;
  std::vector<Item> items = {};
  int width = 0;
  int height = 0;
  tgfx::Paint redPaint = {};
  tgfx::Paint greenPaint = {};
  tgfx::Paint bluePaint = {};
  size_t itemCount = 0;

  void initItemData();

  void updateItemData();

  void drawContents(tgfx::Canvas* canvas);
};

}  // namespace hello2d
