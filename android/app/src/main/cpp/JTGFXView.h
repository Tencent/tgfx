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

namespace hello2d {
class JTGFXView {
 public:
  explicit JTGFXView(ANativeWindow* nativeWindow,
                     std::shared_ptr<tgfx::Window> window,
                     std::shared_ptr<tgfx::Data> imageBytes, float density)
      : nativeWindow(nativeWindow),
        window(std::move(window)),
        imageBytes(std::move(imageBytes)),
        density(density) {
    updateSize();
  }

  ~JTGFXView() {
    ANativeWindow_release(nativeWindow);
  }

  void updateSize();

  void draw();

 private:
  void createSurface();

  void drawBackground(tgfx::Canvas* canvas);
  void drawShape(tgfx::Canvas* canvas);
  void drawImage(tgfx::Canvas* canvas);

  ANativeWindow* nativeWindow = nullptr;
  std::shared_ptr<tgfx::Window> window;
  std::shared_ptr<tgfx::Surface> surface;
  std::shared_ptr<tgfx::Data> imageBytes;
  int _width = 0;
  int _height = 0;
  float density = 1.0f;
  int drawCount = 0;
};
}
