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
    if (window && (width > 0 && height > 0)) {
      window->invalidSize();
    }
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

bool TGFXBaseView::draw(int drawIndex, float zoom, float offsetX, float offsetY) {
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
  // Calculate base scale and offset to fit 720x720 design size to window
  static constexpr float DESIGN_SIZE = 720.0f;
  auto scaleX = static_cast<float>(surface->width()) / DESIGN_SIZE;
  auto scaleY = static_cast<float>(surface->height()) / DESIGN_SIZE;
  auto baseScale = std::min(scaleX, scaleY);
  auto scaledSize = DESIGN_SIZE * baseScale;
  auto baseOffsetX = (static_cast<float>(surface->width()) - scaledSize) * 0.5f;
  auto baseOffsetY = (static_cast<float>(surface->height()) - scaledSize) * 0.5f;

  // Apply user zoom and offset on top of the base scale/offset.
  displayList.setZoomScale(zoom * baseScale);
  displayList.setContentOffset(baseOffsetX + offsetX, baseOffsetY + offsetY);

  // Check if content has changed before rendering
  bool needsRender = displayList.hasContentChanged();

  // In delayed one-frame present mode:
  // - If no content changed AND no last recording to submit -> skip rendering
  if (!needsRender && lastRecording == nullptr) {
    device->unlock();
    return false;
  }

  // If no new content but have last recording, only submit it without new rendering
  if (!needsRender) {
    context->submit(std::move(lastRecording));
    window->present(context);
    device->unlock();
    return false;
  }

  // Draw background
  auto canvas = surface->getCanvas();
  canvas->clear();
  int width = 0;
  int height = 0;
  emscripten_get_canvas_element_size(canvasID.c_str(), &width, &height);
  auto density = static_cast<float>(surface->width()) / static_cast<float>(width);
  DrawBackground(canvas, surface->width(), surface->height(), density);

  // Render DisplayList
  displayList.render(surface.get(), false);

  // Delayed one-frame present mode: flush + submit
  auto recording = context->flush();
  if (lastRecording) {
    context->submit(std::move(lastRecording));
    if (recording) {
      window->present(context);
    }
  }
  lastRecording = std::move(recording);

  device->unlock();

  // In delayed one-frame mode, if we have a pending recording, we need another frame to present it
  // So return true to keep the render loop running
  return displayList.hasContentChanged() || lastRecording != nullptr;
}

}  // namespace hello2d

int main(int, const char*[]) {
  return 0;
}
