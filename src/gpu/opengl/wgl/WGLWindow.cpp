/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
std::shared_ptr<WGLWindow> WGLWindow::MakeFrom(HWND hWnd, HGLRC sharedContext) {
  if (hWnd == nullptr) {
    return nullptr;
  }

  auto device = WGLDevice::MakeFrom(hWnd, sharedContext);
  if (device == nullptr) {
    return nullptr;
  }
  auto wglWindow = std::shared_ptr<WGLWindow>(new WGLWindow(device));
  wglWindow->nativeWindow = hWnd;
  return wglWindow;
}

WGLWindow::WGLWindow(std::shared_ptr<Device> device) : Window(std::move(device)) {
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
  return Surface::MakeFrom(context, renderTarget, ImageOrigin::BottomLeft);
}

void WGLWindow::onPresent(Context* context, int64_t presentationTime) {
  auto device = std::static_pointer_cast<WGLDevice>(this->device);
  SwapBuffers(device->deviceContext);
}

}  // namespace tgfx
