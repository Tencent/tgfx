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

#pragma once

#include <android/native_window_jni.h>
#include <jni.h>
#include <string>
#include "hello2d/AppHost.h"
#include "hello2d/LayerBuilder.h"
#include "tgfx/gpu/Recording.h"
#include "tgfx/gpu/Window.h"
#include "tgfx/gpu/opengl/egl/EGLWindow.h"
#include "tgfx/layers/DisplayList.h"

namespace hello2d {
class JTGFXView {
 public:
  explicit JTGFXView(ANativeWindow* nativeWindow, std::shared_ptr<tgfx::Window> window,
                     std::unique_ptr<hello2d::AppHost> appHost)
      : nativeWindow(nativeWindow), window(std::move(window)), appHost(std::move(appHost)) {
    displayList.setRenderMode(tgfx::RenderMode::Tiled);
    displayList.setAllowZoomBlur(true);
    displayList.setMaxTileCount(512);
    updateSize();
  }

  ~JTGFXView() {
    ANativeWindow_release(nativeWindow);
  }

  void updateSize();
  void updateDisplayList(int drawIndex);
  void updateDisplayTransform(float zoom, float offsetX, float offsetY);
  void draw();

 private:
  void applyCenteringTransform();

  ANativeWindow* nativeWindow = nullptr;
  std::shared_ptr<tgfx::Window> window;
  std::unique_ptr<hello2d::AppHost> appHost = nullptr;
  tgfx::DisplayList displayList = {};
  std::shared_ptr<tgfx::Layer> contentLayer = nullptr;
  std::unique_ptr<tgfx::Recording> lastRecording = nullptr;
  int lastDrawIndex = -1;
  int lastSurfaceWidth = 0;
  int lastSurfaceHeight = 0;
};
}  // namespace hello2d
