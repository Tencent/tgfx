/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include <emscripten/html5.h>
#include <cmath>
#include "hello2d/LayerBuilder.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Surface.h"
#ifdef TGFX_USE_WEBGPU
#include "gpu/webgpu/WebGPUBuffer.h"
#endif

using namespace emscripten;
namespace hello2d {

TGFXBaseView::TGFXBaseView(const std::string& canvasID) : canvasID(canvasID) {
  appHost = std::make_shared<hello2d::AppHost>();
  displayList.setRenderMode(tgfx::RenderMode::Tiled);
  displayList.setTileUpdateMode(tgfx::TileUpdateMode::Smooth);
  displayList.setMaxTileCount(512);
}

void TGFXBaseView::updateSize() {
  if (window == nullptr) {
#ifdef TGFX_USE_WEBGPU
    window = tgfx::WebGPUWindow::MakeFrom(canvasID);
#else
    window = tgfx::WebGLWindow::MakeFrom(canvasID);
#endif
  }
  if (window == nullptr) {
    return;
  }
  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return;
  }
  surface = tgfx::Surface::MakeFrom(context, window);
  if (surface == nullptr) {
    device->unlock();
    return;
  }
  if (surface->width() != lastSurfaceWidth || surface->height() != lastSurfaceHeight) {
    lastSurfaceWidth = surface->width();
    lastSurfaceHeight = surface->height();
    applyCenteringTransform();
    presentImmediately = true;
  }
  device->unlock();
}

void TGFXBaseView::setImagePath(const std::string& name, tgfx::NativeImageRef nativeImage) {
  auto image = tgfx::Image::MakeFrom(nativeImage);
  if (image) {
    appHost->addImage(name, std::move(image));
  }
}

void TGFXBaseView::applyCenteringTransform() {
  if (lastSurfaceWidth > 0 && lastSurfaceHeight > 0 && contentLayer) {
    hello2d::LayerBuilder::ApplyCenteringTransform(
        contentLayer, static_cast<float>(lastSurfaceWidth), static_cast<float>(lastSurfaceHeight));
  }
}

void TGFXBaseView::updateZoomScaleAndOffset(float zoom, float offsetX, float offsetY) {
  displayList.setZoomScale(zoom);
  displayList.setContentOffset(offsetX, offsetY);
}

void TGFXBaseView::updateLayerTree(int drawIndex) {
  auto numBuilders = hello2d::LayerBuilder::Count();
  auto index = drawIndex % numBuilders;
  if (index != lastDrawIndex || !contentLayer) {
    auto builder = hello2d::LayerBuilder::GetByIndex(index);
    if (builder) {
      contentLayer = builder->buildLayerTree(appHost.get());
      if (contentLayer) {
        displayList.root()->removeChildren();
        displayList.root()->addChild(contentLayer);
        applyCenteringTransform();
      }
    }
    lastDrawIndex = index;
  }
}

void TGFXBaseView::draw() {
  if (window == nullptr) {
#ifdef TGFX_USE_WEBGPU
    window = tgfx::WebGPUWindow::MakeFrom(canvasID);
#else
    window = tgfx::WebGLWindow::MakeFrom(canvasID);
#endif
  }
  if (window == nullptr) {
    return;
  }

  bool hasContentChanged = displayList.hasContentChanged();
  bool hasLastRecording = (lastRecording != nullptr);

  if (!hasContentChanged && !hasLastRecording) {
    return;
  }

  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return;
  }

  if (surface == nullptr) {
    surface = tgfx::Surface::MakeFrom(context, window);
  }
  if (surface == nullptr) {
    device->unlock();
    return;
  }

  auto canvas = surface->getCanvas();
  canvas->clear();
  auto cssWidth = static_cast<double>(surface->width());
  auto cssHeight = static_cast<double>(surface->height());
  emscripten_get_element_css_size(canvasID.c_str(), &cssWidth, &cssHeight);
  auto density = static_cast<float>(surface->width()) / static_cast<float>(cssWidth);
  DrawBackground(canvas, surface->width(), surface->height(), density);

  displayList.render(surface.get(), false);

  auto recording = context->flush();

#ifdef TGFX_USE_WEBGPU
  if (recording) {
    context->submit(std::move(recording));
  }
#else
  if (presentImmediately) {
    presentImmediately = false;
    if (recording) {
      context->submit(std::move(recording));
    }
  } else {
    std::swap(lastRecording, recording);

    if (recording) {
      context->submit(std::move(recording));
    }
  }
#endif

  device->unlock();
}

emscripten::val TGFXBaseView::startReadback(int srcX, int srcY, int width, int height) {
  if (window == nullptr || surface == nullptr) {
    return emscripten::val::null();
  }
  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return emscripten::val::null();
  }

  auto rect = tgfx::Rect::MakeXYWH(srcX, srcY, width, height);
  pendingReadback = surface->asyncReadPixels(rect);
  if (pendingReadback == nullptr) {
    device->unlock();
    return emscripten::val::null();
  }

  // Flush and submit GPU commands (but don't wait for completion).
  auto recording = context->flush();
  if (recording) {
    context->submit(std::move(recording), false);
  }

  // Get the underlying WebGPU buffer handle for JS-side mapAsync.
  auto info = pendingReadback->info();
#ifdef TGFX_USE_WEBGPU
  auto gpuBuffer = pendingReadback->getGPUBuffer(context);
  if (gpuBuffer == nullptr) {
    pendingReadback = nullptr;
    device->unlock();
    return emscripten::val::null();
  }
  auto result = emscripten::val::object();
  result.set("width", info.width());
  result.set("height", info.height());
  result.set("rowBytes", static_cast<int>(info.rowBytes()));
  result.set(
      "bufferHandle",
      reinterpret_cast<int>(static_cast<tgfx::WebGPUBuffer*>(gpuBuffer.get())->webgpuBuffer()));
  result.set("bufferSize", static_cast<int>(gpuBuffer->size()));
  result.set("ready", false);
  device->unlock();
  return result;
#else
  // WebGL: readback is synchronous, just do it immediately.
  auto pixels =
      pendingReadback->lockPixels(context, surface->origin() == tgfx::ImageOrigin::BottomLeft);
  if (pixels == nullptr) {
    pendingReadback = nullptr;
    device->unlock();
    return emscripten::val::null();
  }
  auto byteSize = static_cast<size_t>(info.height()) * info.rowBytes();
  auto uint8Array = emscripten::val::global("Uint8Array").new_(byteSize);
  auto memory = emscripten::val::module_property("HEAPU8")["buffer"];
  auto view = emscripten::val::global("Uint8Array")
                  .new_(memory, reinterpret_cast<uintptr_t>(pixels), byteSize);
  uint8Array.call<void>("set", view);
  pendingReadback->unlockPixels(context);
  pendingReadback = nullptr;
  auto result = emscripten::val::object();
  result.set("width", info.width());
  result.set("height", info.height());
  result.set("rowBytes", static_cast<int>(info.rowBytes()));
  result.set("pixels", uint8Array);
  result.set("ready", true);
  device->unlock();
  return result;
#endif
}

emscripten::val TGFXBaseView::finishReadback() {
  if (window == nullptr || pendingReadback == nullptr) {
    return emscripten::val::null();
  }
  auto device = window->getDevice();
  auto context = device->lockContext();
  if (context == nullptr) {
    return emscripten::val::null();
  }

#ifdef TGFX_USE_WEBGPU
  // JS already called mapAsync and it resolved. Mark buffer as ready so lockPixels skips
  // requestMapAsync (which would trigger Asyncify).
  auto gpuBuffer = pendingReadback->getGPUBuffer(context);
  if (gpuBuffer != nullptr) {
    static_cast<tgfx::WebGPUBuffer*>(gpuBuffer.get())->setMapReady(true);
  }
  auto info = pendingReadback->info();
  auto pixels =
      pendingReadback->lockPixels(context, surface->origin() == tgfx::ImageOrigin::BottomLeft);
  if (pixels == nullptr) {
    pendingReadback = nullptr;
    device->unlock();
    return emscripten::val::null();
  }
  auto byteSize = static_cast<size_t>(info.height()) * info.rowBytes();
  auto uint8Array = emscripten::val::global("Uint8Array").new_(byteSize);
  auto memory = emscripten::val::module_property("HEAPU8")["buffer"];
  auto view = emscripten::val::global("Uint8Array")
                  .new_(memory, reinterpret_cast<uintptr_t>(pixels), byteSize);
  uint8Array.call<void>("set", view);
  pendingReadback->unlockPixels(context);
  pendingReadback = nullptr;
  device->unlock();
  return uint8Array;
#else
  // WebGL path: should not reach here (startReadback returns ready=true with pixels).
  pendingReadback = nullptr;
  device->unlock();
  return emscripten::val::null();
#endif
}

}  // namespace hello2d

int main(int, const char*[]) {
  return 0;
}
