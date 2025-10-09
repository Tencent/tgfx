/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "hello2d/AppHost.h"
#include "hello2d/LayerBuilder.h"
#include "tgfx/gpu/opengl/webgl/WebGLWindow.h"

namespace hello2d {

class TGFXBaseView {
 public:
  TGFXBaseView(const std::string& canvasID);

  void setImagePath(const std::string& name, tgfx::NativeImageRef nativeImage);

  void updateSize(float devicePixelRatio);

  bool draw(int drawIndex, float zoom, float offsetX, float offsetY);

  void onWheelEvent();
  void onClickEvent();

 protected:
  std::shared_ptr<hello2d::AppHost> appHost;

 private:
  std::string canvasID = "";
  std::shared_ptr<tgfx::Window> window = nullptr;
};

}  // namespace hello2d
