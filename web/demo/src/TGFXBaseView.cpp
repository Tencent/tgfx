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
      // Clear lastRecording when size changes, as it was created for the old surface size
      lastRecording = nullptr;
      // Reset cached surface size to force recalculation in draw()
      lastSurfaceWidth = 0;
      lastSurfaceHeight = 0;
      // Mark size as invalidated to force render next frame
      sizeInvalidated = true;
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

void TGFXBaseView::applyTransform() {
  if (lastSurfaceWidth > 0 && lastSurfaceHeight > 0) {
    static constexpr float DESIGN_SIZE = 720.0f;
    auto scaleX = static_cast<float>(lastSurfaceWidth) / DESIGN_SIZE;
    auto scaleY = static_cast<float>(lastSurfaceHeight) / DESIGN_SIZE;
    auto baseScale = std::min(scaleX, scaleY);
    auto scaledSize = DESIGN_SIZE * baseScale;
    auto baseOffsetX = (static_cast<float>(lastSurfaceWidth) - scaledSize) * 0.5f;
    auto baseOffsetY = (static_cast<float>(lastSurfaceHeight) - scaledSize) * 0.5f;

    displayList.setZoomScale(currentZoom * baseScale);
    displayList.setContentOffset(baseOffsetX + currentOffsetX, baseOffsetY + currentOffsetY);
  }
}

void TGFXBaseView::updateDrawParams(int drawIndex, float zoom, float offsetX, float offsetY) {
  // Cache current parameters for use when size changes
  currentZoom = zoom;
  currentOffsetX = offsetX;
  currentOffsetY = offsetY;

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

  // Apply transform using cached surface size
  applyTransform();
}

bool TGFXBaseView::draw() {
  // Initialize window if needed
  if (window == nullptr) {
    window = tgfx::WebGLWindow::MakeFrom(canvasID);
  }
  if (window == nullptr) {
    return false;
  }

  // Check if content has changed at the VERY BEGINNING, before locking device
  // If no content change AND no pending lastRecording -> skip everything
  bool needsRender = displayList.hasContentChanged() || sizeInvalidated;
  if (!needsRender && lastRecording == nullptr) {
    return false;
  }

  // ========== Now lock device for rendering/submitting ==========

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

  // Update cached surface size and recalculate transform if size changed
  int newSurfaceWidth = surface->width();
  int newSurfaceHeight = surface->height();
  bool sizeChanged = (newSurfaceWidth != lastSurfaceWidth || newSurfaceHeight != lastSurfaceHeight);
  if (sizeChanged) {
    lastSurfaceWidth = newSurfaceWidth;
    lastSurfaceHeight = newSurfaceHeight;
    // Recalculate transform with new surface size
    applyTransform();
    needsRender = true;
  }

  // Clear sizeInvalidated flag now that we've handled size changes
  sizeInvalidated = false;

  // Track if we submitted anything this frame
  bool didSubmit = false;

  // Case 1: No content change BUT have pending lastRecording -> only submit lastRecording
  if (!needsRender) {
    context->submit(std::move(lastRecording));
    window->present(context);
    didSubmit = true;
    device->unlock();
    return didSubmit;
  }

  // Case 2: Content changed -> render new content
  auto canvas = surface->getCanvas();
  canvas->clear();
  int width = 0;
  int height = 0;
  emscripten_get_canvas_element_size(canvasID.c_str(), &width, &height);
  auto density = static_cast<float>(surface->width()) / static_cast<float>(width);
  DrawBackground(canvas, surface->width(), surface->height(), density);

  // Render DisplayList
  displayList.render(surface.get(), false);

  // Delayed one-frame present mode
  auto recording = context->flush();
  if (lastRecording) {
    context->submit(std::move(lastRecording));
    window->present(context);
    didSubmit = true;
    lastRecording = std::move(recording);
  } else if (recording) {
    context->submit(std::move(recording));
    window->present(context);
    didSubmit = true;
  }

  device->unlock();

  return didSubmit || lastRecording != nullptr;
}

}  // namespace hello2d

int main(int, const char*[]) {
  return 0;
}
