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
#include <emscripten.h>
#include "core/utils/Log.h"
#include "gpu/webgpu/WebGPUGPU.h"
#include "tgfx/gpu/opengl/webgl/WebGLWindow.h"

namespace tgfx {
std::shared_ptr<Window> WebGPUWindow::MakeFrom(const std::string& canvasID,
                                               std::shared_ptr<ColorSpace> colorSpace) {
  if (canvasID.empty()) {
    return nullptr;
  }

  bool isWebGPUSupported = emscripten::val::module_property("tgfx").call<bool>("isWebGPUSupported");
  if (!isWebGPUSupported) {
    LOGI("WebGPU is not supported, falling back to WebGL.");
    return WebGLWindow::MakeFrom(canvasID, colorSpace);
  }

  auto device = WebGPUDevice::MakeFrom(canvasID, colorSpace);
  if (device == nullptr) {
    return nullptr;
  }
  auto window = std::shared_ptr<WebGPUWindow>(new WebGPUWindow(device, std::move(colorSpace)));
  window->canvasID = canvasID;
  return window;
}

WebGPUWindow::WebGPUWindow(std::shared_ptr<Device> device, std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device)), colorSpace(std::move(colorSpace)) {
}

std::shared_ptr<Surface> WebGPUWindow::onCreateSurface(Context* context) {
  auto webgpuDevice = std::static_pointer_cast<WebGPUDevice>(device);
  auto wgpuSurface = webgpuDevice->wgpuSurface();
  if (wgpuSurface == nullptr) {
    return nullptr;
  }

  // Get canvas size and configure surface if size changed
  int width = 0;
  int height = 0;
  emscripten_get_canvas_element_size(canvasID.c_str(), &width, &height);
  if (width <= 0 || height <= 0) {
    LOGE("WebGPUWindow::onCreateSurface() invalid canvas size: %dx%d", width, height);
    return nullptr;
  }
  webgpuDevice->configureSurface(width, height);

  // Get current texture from surface
  wgpu::SurfaceTexture surfaceTexture = {};
  wgpuSurface.GetCurrentTexture(&surfaceTexture);
  if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::Success) {
    LOGE("WebGPUWindow::onCreateSurface() failed to get current texture, status: %d",
         static_cast<int>(surfaceTexture.status));
    return nullptr;
  }
  if (surfaceTexture.texture == nullptr) {
    LOGE("WebGPUWindow::onCreateSurface() surface texture is null");
    return nullptr;
  }

  // Wrap texture into BackendRenderTarget
  WebGPUTextureInfo textureInfo = {};
  textureInfo.texture = surfaceTexture.texture.Get();
  textureInfo.format = static_cast<unsigned>(webgpuDevice->surfaceFormat());

  BackendRenderTarget renderTarget(textureInfo, width, height);
  return Surface::MakeFrom(context, renderTarget, ImageOrigin::TopLeft, 0, colorSpace);
}

void WebGPUWindow::onPresent(Context*) {
  auto webgpuDevice = std::static_pointer_cast<WebGPUDevice>(device);
  auto wgpuSurface = webgpuDevice->wgpuSurface();
  if (wgpuSurface != nullptr) {
    wgpuSurface.Present();
  }
}

}  // namespace tgfx