/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/gpu/webgpu/WebGPUWindow.h"
#include <emscripten.h>
#include "core/utils/Log.h"
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

std::shared_ptr<Surface> WebGPUWindow::onCreateSurface(Context* /*context*/) {

  return nullptr;
}

}  // namespace tgfx