/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/gpu/webgpu/WebGPUWindow.h"
#include <webgpu/webgpu.h>
#include "WebGPUDefines.h"
#include "WebGPUDrawableProxy.h"
#include "WebGPUGPU.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#include <emscripten/html5_webgpu.h>
#endif

namespace tgfx {

std::shared_ptr<WebGPUWindow> WebGPUWindow::MakeFrom(const std::string& canvasSelector,
                                                     std::shared_ptr<WebGPUDevice> device) {
  if (canvasSelector.empty()) {
    return nullptr;
  }
  if (device == nullptr) {
    device = WebGPUDevice::Make();
  }
  if (device == nullptr) {
    return nullptr;
  }

  WGPUSurface surface = nullptr;
#ifdef __EMSCRIPTEN__
  WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc = {};
  canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
  canvasDesc.selector = canvasSelector.c_str();

  WGPUSurfaceDescriptor surfaceDesc = {};
  surfaceDesc.nextInChain = reinterpret_cast<const WGPUChainedStruct*>(&canvasDesc);

  auto wgpuInstance = wgpuCreateInstance(nullptr);
  surface = wgpuInstanceCreateSurface(wgpuInstance, &surfaceDesc);
  if (wgpuInstance != nullptr) {
    wgpuInstanceRelease(wgpuInstance);
  }
#endif

  if (surface == nullptr) {
    return nullptr;
  }

  auto wgpuDevice = static_cast<WGPUDevice>(device->webgpuDevice());

  // Configure the surface.
  int canvasWidth = 0;
  int canvasHeight = 0;
#ifdef __EMSCRIPTEN__
  emscripten_get_canvas_element_size(canvasSelector.c_str(), &canvasWidth, &canvasHeight);
#endif
  if (canvasWidth <= 0 || canvasHeight <= 0) {
    wgpuSurfaceRelease(surface);
    return nullptr;
  }

  WGPUSurfaceConfiguration config = {};
  config.device = wgpuDevice;
  config.format = WGPUTextureFormat_BGRA8Unorm;
  config.usage = WGPUTextureUsage_RenderAttachment;
  config.width = static_cast<uint32_t>(canvasWidth);
  config.height = static_cast<uint32_t>(canvasHeight);
  config.presentMode = WGPUPresentMode_Fifo;
  config.alphaMode = WGPUCompositeAlphaMode_Premultiplied;
  wgpuSurfaceConfigure(surface, &config);

  return std::shared_ptr<WebGPUWindow>(
      new WebGPUWindow(std::move(device), surface, canvasWidth, canvasHeight));
}

WebGPUWindow::WebGPUWindow(std::shared_ptr<Device> device, void* surface, int width, int height)
    : Window(std::move(device)), _surface(surface), _width(width), _height(height) {
}

std::shared_ptr<RenderTargetProxy> WebGPUWindow::onCreateRenderTarget(Context* context) {
  if (_surface == nullptr || _width <= 0 || _height <= 0) {
    return nullptr;
  }

  // Re-query canvas size in case it changed.
#ifdef __EMSCRIPTEN__
  // For Emscripten, we could re-query the canvas size here if needed.
  // For now, use the stored dimensions.
#endif

  auto wgpuSurface = static_cast<WGPUSurface>(_surface);
  drawableProxy = std::make_shared<WebGPUDrawableProxy>(
      context, _width, _height, wgpuSurface, WGPUTextureFormat_BGRA8Unorm, PixelFormat::BGRA_8888);
  return drawableProxy;
}

void WebGPUWindow::onPresent(Context*) {
  if (drawableProxy == nullptr) {
    return;
  }
  auto proxy = std::static_pointer_cast<WebGPUDrawableProxy>(drawableProxy);
  proxy->present();
  proxy->releaseDrawable();
}

}  // namespace tgfx
