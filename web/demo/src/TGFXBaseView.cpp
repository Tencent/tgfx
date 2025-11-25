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

#include "TGFXBaseView.h"
#include <cmath>
#include "hello2d/LayerBuilder.h"
#include "tgfx/core/Point.h"

using namespace emscripten;
namespace hello2d {

TGFXBaseView::TGFXBaseView(const std::string& canvasID) : canvasID(canvasID) {
  appHost = std::make_shared<hello2d::AppHost>();
  // Initialize DisplayList with tiled rendering mode
  displayList.setRenderMode(tgfx::RenderMode::Tiled);
  displayList.setAllowZoomBlur(true);
  displayList.setMaxTileCount(512);
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
    needsRedraw = true;
  }
}

void TGFXBaseView::setImagePath(const std::string& name, tgfx::NativeImageRef nativeImage) {
  auto image = tgfx::Image::MakeFrom(nativeImage);
  if (image) {
    appHost->addImage(name, std::move(image));
  }
  needsRedraw = true;
}

void TGFXBaseView::onWheelEvent() {
  needsRedraw = true;
}

void TGFXBaseView::onClickEvent() {
  needsRedraw = true;
}

bool TGFXBaseView::draw(int drawIndex, float zoom, float offsetX, float offsetY) {
  if (!needsRedraw) {
    return true;
  }

  if (appHost->width() <= 0 || appHost->height() <= 0) {
    return true;
  }

  // Initialize window if needed
  if (window == nullptr) {
    window = tgfx::WebGLWindow::MakeFrom(canvasID);
  }
  if (window == nullptr) {
    return true;
  }

  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return true;
  }

  auto surface = window->getSurface(context);
  if (surface == nullptr) {
    device->unlock();
    return true;
  }

  // Switch sample when drawIndex changes
  if (drawIndex != lastDrawIndex) {
    auto layer = BuildAndCenterLayer(drawIndex % GetLayerBuilderCount(), appHost.get());
    if (layer) {
      displayList.root()->removeChildren();
      displayList.root()->addChild(layer);
    }
    lastDrawIndex = drawIndex;
  }

  // Directly set zoom and offset on DisplayList
  displayList.setZoomScale(zoom);
  displayList.setContentOffset(offsetX, offsetY);

  // Draw background and render DisplayList
  auto canvas = surface->getCanvas();
  canvas->clear();
  DrawSampleBackground(canvas, appHost.get());
  displayList.render(surface.get(), false);

  context->flushAndSubmit();
  window->present(context);
  device->unlock();

  needsRedraw = false;
  return true;
}

}  // namespace hello2d

int main(int, const char*[]) {
  return 0;
}
