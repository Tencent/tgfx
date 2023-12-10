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

#include "TGFXView.h"

namespace hello2d {
TGFXView::TGFXView(std::string canvasID, emscripten::val nativeImage)
    : canvasID(std::move(canvasID)) {
  appHost = std::make_shared<drawers::AppHost>();
  auto image = tgfx::Image::MakeFrom(nativeImage);
  appHost->addImage("bridge", std::move(image));
  // To utilize a custom typeface, you can load it directly into the browser and access it using
  // tgfx::Typeface::MakeFromName().
  auto typeface = tgfx::Typeface::MakeFromName("Arial", "Regular");
  appHost->addTypeface("default", std::move(typeface));
  typeface = tgfx::Typeface::MakeFromName("Noto Color Emoji", "Regular");
  appHost->addTypeface("emoji", std::move(typeface));
}

void TGFXView::updateSize(float devicePixelRatio) {
  if (!canvasID.empty()) {
    int width = 0;
    int height = 0;
    emscripten_get_canvas_element_size(canvasID.c_str(), &width, &height);
    auto sizeChanged = appHost->updateScreen(width, height, devicePixelRatio);
    if (sizeChanged) {
      window->invalidSize();
    }
  }
}

void TGFXView::draw(int drawIndex) {
  if (appHost->width() <= 0 || appHost->height() <= 0) {
    return;
  }
  if (window == nullptr) {
    window = tgfx::WebGLWindow::MakeFrom(canvasID);
  }
  if (window == nullptr) {
    return;
  }
  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return;
  }
  auto surface = window->getSurface(context);
  if (surface == nullptr) {
    device->unlock();
    return;
  }
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto numDrawers = drawers::Drawer::Count() - 1;
  auto index = (drawIndex % numDrawers) + 1;
  auto drawer = drawers::Drawer::GetByName("GridBackground");
  drawer->draw(canvas, appHost.get());
  drawer = drawers::Drawer::GetByIndex(index);
  drawer->draw(canvas, appHost.get());
  surface->flush();
  context->submit();
  window->present(context);
  device->unlock();
}
}  // namespace hello2d

using namespace hello2d;
using namespace emscripten;

EMSCRIPTEN_BINDINGS(TGFXDemo) {
  class_<TGFXView>("TGFXView")
      .smart_ptr<std::shared_ptr<TGFXView>>("TGFXView")
      .class_function("MakeFrom", optional_override(
                                      [](const std::string& canvasID, emscripten::val nativeImage) {
                                        if (canvasID.empty()) {
                                          return std::shared_ptr<TGFXView>(nullptr);
                                        }
                                        return std::make_shared<TGFXView>(canvasID, nativeImage);
                                      }))
      .function("updateSize", &TGFXView::updateSize)
      .function("draw", &TGFXView::draw);
}
