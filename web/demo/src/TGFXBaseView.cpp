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

#include "TGFXBaseView.h"

using namespace emscripten;
namespace hello2d {

TGFXBaseView::TGFXBaseView(std::string canvasID, const val& nativeImage)
    : canvasID(std::move(canvasID)) {
  appHost = std::make_shared<drawers::AppHost>();
  auto image = tgfx::Image::MakeFrom(nativeImage);
  if (image) {
    appHost->addImage("bridge", std::move(image));
  }
}

void TGFXBaseView::updateSize(float devicePixelRatio) {
  if (!canvasID.empty()) {
    int width = 0;
    int height = 0;
    emscripten_get_canvas_element_size(canvasID.c_str(), &width, &height);
    auto sizeChanged = appHost->updateScreen(width, height, devicePixelRatio);
    if (sizeChanged && window) {
      window->invalidSize();
    }
  }
}

void TGFXBaseView::draw(int drawIndex) {
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
  if (onDraw(surface.get(), appHost.get(), drawIndex)) {
    context->flushAndSubmit();
    window->present(context);
  }
  device->unlock();
}

bool TGFXBaseView::onDraw(tgfx::Surface* surface, const drawers::AppHost* appHost, int drawIndex) {
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto numDrawers = drawers::Drawer::Count() - 1;
  auto index = (drawIndex % numDrawers) + 1;
  auto drawer = drawers::Drawer::GetByName("GridBackground");
  drawer->draw(canvas, appHost);
  drawer = drawers::Drawer::GetByIndex(index);
  drawer->draw(canvas, appHost);
  return true;
}
}  // namespace hello2d

int main(int, const char*[]) {
  return 0;
}
