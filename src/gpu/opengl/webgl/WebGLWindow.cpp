/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/gpu/opengl/webgl/WebGLWindow.h"
#include <emscripten.h>
#include "core/utils/Log.h"
#include "gpu/opengl/GLDefines.h"

namespace tgfx {
std::shared_ptr<WebGLWindow> WebGLWindow::MakeFrom(const std::string& canvasID,
                                                   std::shared_ptr<ColorSpace> colorSpace) {
  if (canvasID.empty()) {
    return nullptr;
  }
  auto device = WebGLDevice::MakeFrom(canvasID);
  if (device == nullptr) {
    return nullptr;
  }
  auto window = std::shared_ptr<WebGLWindow>(new WebGLWindow(device, std::move(colorSpace)));
  window->canvasID = canvasID;
  return window;
}

WebGLWindow::WebGLWindow(std::shared_ptr<Device> device, std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device), std::move(colorSpace)) {
  if(colorSpace != nullptr && !ColorSpace::Equals(colorSpace.get(), ColorSpace::SRGB().get() &&
		!ColorSpace::Equals(colorSpace.get(), ColorSpace::DisplayP3().get())){
    this->colorSpace = nullptr;
  }
}

std::shared_ptr<Surface> WebGLWindow::onCreateSurface(Context* context) {
  int width = 0;
  int height = 0;
  emscripten_get_canvas_element_size(canvasID.c_str(), &width, &height);
  if (width <= 0 || height <= 0) {
    LOGE("WebGLWindow::onCreateSurface() Can not create a Surface with zero size.");
    return nullptr;
  }
  GLFrameBufferInfo glInfo = {};
  glInfo.id = 0;
  glInfo.format = GL_RGBA8;
  return Surface::MakeFrom(context, {glInfo, width, height}, ImageOrigin::BottomLeft, 0,
                           colorSpace);
}
}  // namespace tgfx
