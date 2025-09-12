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
#include "tgfx/gpu/opengl/GLDefines.h"

EM_JS(int, getBrowserColorGamut, (), {
  if (window.matchMedia("(color-gamut: rec2020)").matches) return 2;
  else if (window.matchMedia("(color-gamut: p3)").matches)
    return 1;
  return 0;
});

namespace tgfx {
std::shared_ptr<WebGLWindow> WebGLWindow::MakeFrom(const std::string& canvasID) {
  if (canvasID.empty()) {
    return nullptr;
  }
  auto device = WebGLDevice::MakeFrom(canvasID);
  if (device == nullptr) {
    return nullptr;
  }
  auto window = std::shared_ptr<WebGLWindow>(new WebGLWindow(device));
  window->canvasID = canvasID;
  return window;
}

WebGLWindow::WebGLWindow(std::shared_ptr<Device> device) : Window(std::move(device)) {
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
  int gamut = getBrowserColorGamut();
  std::shared_ptr<ColorSpace> colorSpace = nullptr;
  switch (gamut) {
    case 2:
      colorSpace = ColorSpace::MakeRGB(namedTransferFn::Rec2020, namedGamut::Rec2020);
      break;
    case 1:
      colorSpace = ColorSpace::MakeRGB(namedTransferFn::SRGB, namedGamut::DisplayP3);
      break;
    default:
      colorSpace = ColorSpace::MakeSRGB();
      break;
  }
  return Surface::MakeFrom(context, {glInfo, width, height}, ImageOrigin::BottomLeft, 1,
                           colorSpace);
}
}  // namespace tgfx
