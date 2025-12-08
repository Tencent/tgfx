/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/gpu/opengl/wgl/WGLWindow.h"
#include <GL/GL.h>
#include "core/utils/Log.h"

namespace tgfx {
std::shared_ptr<WGLWindow> WGLWindow::MakeFrom(HWND nativeWindow, HGLRC sharedContext,
                                               std::shared_ptr<ColorSpace> colorSpace) {
  if (nativeWindow == nullptr) {
    return nullptr;
  }

  auto device = WGLDevice::MakeFrom(nativeWindow, sharedContext);
  if (device == nullptr) {
    return nullptr;
  }
  if (colorSpace != nullptr && !ColorSpace::Equals(colorSpace.get(), ColorSpace::SRGB().get())) {
    LOGW(
        "The current platform does not support the colorspace, which may cause color inaccuracies "
        "on Window.");
  }
  auto wglWindow = std::shared_ptr<WGLWindow>(new WGLWindow(device, std::move(colorSpace)));
  wglWindow->nativeWindow = nativeWindow;
  return wglWindow;
}

WGLWindow::WGLWindow(std::shared_ptr<Device> device, std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device), std::move(colorSpace)) {
}

std::shared_ptr<Surface> WGLWindow::onCreateSurface(Context* context) {
  ISize size = {0, 0};
  if (nativeWindow) {
    RECT rect = {};
    GetClientRect(nativeWindow, &rect);
    size.width = static_cast<int>(rect.right - rect.left);
    size.height = static_cast<int>(rect.bottom - rect.top);
  }
  if (size.width <= 0 || size.height <= 0) {
    return nullptr;
  }

  GLFrameBufferInfo frameBuffer = {0, GL_RGBA8};
  BackendRenderTarget renderTarget = {frameBuffer, size.width, size.height};
  return Surface::MakeFrom(context, renderTarget, ImageOrigin::BottomLeft, 0, colorSpace);
}

void WGLWindow::onPresent(Context*) {
  const auto wglDevice = std::static_pointer_cast<WGLDevice>(this->device);
  SwapBuffers(wglDevice->deviceContext);
}

}  // namespace tgfx
