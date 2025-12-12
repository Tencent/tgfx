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
  displayList.setRenderMode(tgfx::RenderMode::Tiled);
  displayList.setAllowZoomBlur(true);
  displayList.setMaxTileCount(512);
}

void TGFXBaseView::updateSize(float devicePixelRatio) {
  if (canvasID.empty()) {
    return;
  }
  int width = 0;
  int height = 0;
  emscripten_get_canvas_element_size(canvasID.c_str(), &width, &height);
  if (width <= 0 || height <= 0) {
    return;
  }
  lastSurfaceWidth = static_cast<int>(width * devicePixelRatio);
  lastSurfaceHeight = static_cast<int>(height * devicePixelRatio);
  isResizing = true;
  applyTransform();
  if (window) {
    window->invalidSize();
  }
}

void TGFXBaseView::setImagePath(const std::string& name, tgfx::NativeImageRef nativeImage) {
  auto image = tgfx::Image::MakeFrom(nativeImage);
  if (image) {
    appHost->addImage(name, std::move(image));
  }
}

void TGFXBaseView::onWheelEvent() {
}

void TGFXBaseView::onClickEvent() {
}

void TGFXBaseView::applyTransform() {
  if (lastSurfaceWidth > 0 && lastSurfaceHeight > 0) {
    if (contentLayer) {
      hello2d::LayerBuilder::ApplyCenteringTransform(contentLayer,
                                                     static_cast<float>(lastSurfaceWidth),
                                                     static_cast<float>(lastSurfaceHeight));
    }
    displayList.setZoomScale(currentZoom);
    displayList.setContentOffset(currentOffsetX, currentOffsetY);
  }
}

void TGFXBaseView::updateDrawParams(int drawIndex, float zoom, float offsetX, float offsetY) {
  currentZoom = zoom;
  currentOffsetX = offsetX;
  currentOffsetY = offsetY;

  auto numBuilders = hello2d::LayerBuilder::Count();
  auto index = drawIndex % numBuilders;
  if (index != lastDrawIndex || !contentLayer) {
    auto builder = hello2d::LayerBuilder::GetByIndex(index);
    if (builder) {
      contentLayer = builder->buildLayerTree(appHost.get());
      if (contentLayer) {
        displayList.root()->removeChildren();
        displayList.root()->addChild(contentLayer);
      }
    }
    lastDrawIndex = index;
  }

  applyTransform();
}

bool TGFXBaseView::draw() {
  if (window == nullptr) {
    window = tgfx::WebGLWindow::MakeFrom(canvasID);
  }
  if (window == nullptr) {
    return false;
  }

  if (!displayList.hasContentChanged() && lastRecording == nullptr) {
    return false;
  }

  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return false;
  }

  auto surface = window->getSurface(context);
  if (surface == nullptr) {
    device->unlock();
    return false;
  }

  // Sync surface size for DPI changes.
  bool surfaceResized = isResizing;
  isResizing = false;

  if (surface->width() != lastSurfaceWidth || surface->height() != lastSurfaceHeight) {
    lastSurfaceWidth = surface->width();
    lastSurfaceHeight = surface->height();
    applyTransform();
    surfaceResized = true;
  }

  auto canvas = surface->getCanvas();
  canvas->clear();
  int width = 0;
  int height = 0;
  emscripten_get_canvas_element_size(canvasID.c_str(), &width, &height);
  auto density = static_cast<float>(surface->width()) / static_cast<float>(width);
  DrawBackground(canvas, surface->width(), surface->height(), density);

  displayList.render(surface.get(), false);

  auto recording = context->flush();

  bool submitted = false;
  if (surfaceResized) {
    // When resized, submit current frame immediately (no delay)
    if (recording) {
      context->submit(std::move(recording));
      window->present(context);
      submitted = true;
    }
    lastRecording = nullptr;
  } else {
    // Delayed one-frame present
    std::swap(lastRecording, recording);

    if (recording) {
      context->submit(std::move(recording));
      window->present(context);
      submitted = true;
    }
  }

  device->unlock();
  return submitted || (lastRecording != nullptr);
}

}  // namespace hello2d

int main(int, const char*[]) {
  return 0;
}
