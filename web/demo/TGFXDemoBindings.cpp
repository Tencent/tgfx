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

#include <emscripten/bind.h>
#include "tgfx/opengl/webgl/WebGLWindow.h"

using namespace emscripten;
using namespace tgfx;

class TGFXView {
 public:
  explicit TGFXView(std::string canvasID) : canvasID(std::move(canvasID)) {
    updateSize();
  }

  void updateSize() {
    if (!canvasID.empty()) {
      emscripten_get_canvas_element_size(canvasID.c_str(), &_width, &_height);
      surface = nullptr;
    }
  }

  void draw() {
    if (surface == nullptr) {
      createSurface();
    }
    if (surface == nullptr) {
      return;
    }
    auto device = window->getDevice();
    auto context = device->lockContext();
    if (context == nullptr) {
      return;
    }
    auto canvas = surface->getCanvas();
    canvas->clear();
    tgfx::Paint paint;
    paint.setColor(tgfx::Color{0.0f, 0.8f, 0.0f, 1.f});
    if (drawCount % 2 == 0) {
      auto rect = tgfx::Rect::MakeXYWH(20, 20, 100, 100);
      canvas->drawRect(rect, paint);
    } else {
      int tileSize = 8;
      for (int y = 0; y < _height; y += tileSize) {
        bool draw = (y / tileSize) % 2 == 1;
        for (int x = 0; x < _width; x += tileSize) {
          if (draw) {
            auto rect =
                tgfx::Rect::MakeXYWH(static_cast<float>(x), static_cast<float>(y),
                                     static_cast<float>(tileSize), static_cast<float>(tileSize));
            canvas->drawRect(rect, paint);
          }
          draw = !draw;
        }
      }
    }
    surface->flush();
    context->submit();
    window->present(context);
    device->unlock();
    drawCount++;
  }

 private:
  void createSurface() {
    if (_width <= 0 || _height <= 0) {
      return;
    }
    if (window == nullptr) {
      window = WebGLWindow::MakeFrom(canvasID);
    }
    if (window == nullptr) {
      return;
    }
    auto device = window->getDevice();
    auto context = device->lockContext();
    if (context == nullptr) {
      return;
    }
    surface = window->createSurface(context);
    device->unlock();
  }

  std::string canvasID;
  std::shared_ptr<tgfx::Window> window;
  std::shared_ptr<tgfx::Surface> surface;
  int _width = 0;
  int _height = 0;
  int drawCount = 0;
};

EMSCRIPTEN_BINDINGS(TGFXDemo) {
  class_<TGFXView>("TGFXView")
      .smart_ptr<std::shared_ptr<TGFXView>>("TGFXView")
      .class_function("_Make", optional_override([](const std::string& canvasID) {
                        if (canvasID.empty()) {
                          return std::shared_ptr<TGFXView>(nullptr);
                        }
                        return std::make_shared<TGFXView>(canvasID);
                      }))
      .function("updateSize", &TGFXView::updateSize)
      .function("draw", &TGFXView::draw);
}
