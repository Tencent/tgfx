/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "tgfx/gpu/Recording.h"
#include "tgfx/gpu/opengl/webgl/WebGLWindow.h"
#include "tgfx/gpu/webgpu/WebGPUWindow.h"
#include "tgfx/layers/DisplayList.h"

namespace hello2d {

class TGFXBaseView {
 public:
  TGFXBaseView(const std::string& canvasID);

  void setImagePath(const std::string& name, tgfx::NativeImageRef nativeImage);

  void updateSize();

  void updateLayerTree(int drawIndex);

  void updateZoomScaleAndOffset(float zoom, float offsetX, float offsetY);

  void draw();

 protected:
  std::shared_ptr<hello2d::AppHost> appHost = nullptr;

 private:
  void applyCenteringTransform();

  std::string canvasID = "";
  std::shared_ptr<tgfx::Window> window = nullptr;
  tgfx::DisplayList displayList = {};
  std::shared_ptr<tgfx::Layer> contentLayer = nullptr;
  int lastDrawIndex = -1;
  std::unique_ptr<tgfx::Recording> lastRecording = nullptr;
  int lastSurfaceWidth = 0;
  int lastSurfaceHeight = 0;
  bool presentImmediately = true;
};

}  // namespace hello2d
