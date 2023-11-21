/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <string>
#include <jni.h>
#include <android/native_window_jni.h>
#include "tgfx/gpu/Window.h"
#include "tgfx/opengl/egl/EGLWindow.h"
#include "tdraw/Drawer.h"

namespace hello2d {
class JTGFXView {
 public:
  explicit JTGFXView(ANativeWindow* nativeWindow,
                     std::shared_ptr<tgfx::Window> window, std::unique_ptr<tdraw::AppHost> appHost)
      : nativeWindow(nativeWindow),
        window(std::move(window)),
        appHost(std::move(appHost)) {
    updateSize();
  }

  ~JTGFXView() {
    ANativeWindow_release(nativeWindow);
  }

  void updateSize();

  void draw(int index);

 private:
  void createSurface();

  ANativeWindow* nativeWindow = nullptr;
  std::shared_ptr<tgfx::Window> window;
  std::shared_ptr<tgfx::Surface> surface;
  std::shared_ptr<tdraw::AppHost> appHost;
  int _width = 0;
  int _height = 0;
};
}
