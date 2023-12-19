/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/opengl/egl/EGLWindow.h"

#if defined(__ANDROID__) || defined(ANDROID)
#include <android/native_window.h>
#elif defined(_WIN32)
#include <WinUser.h>
#endif
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include "utils/USE.h"

namespace tgfx {
std::shared_ptr<EGLWindow> EGLWindow::Current() {
  auto device = std::static_pointer_cast<EGLDevice>(GLDevice::Current());
  if (device == nullptr || device->eglSurface == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<EGLWindow>(new EGLWindow(device));
}

std::shared_ptr<EGLWindow> EGLWindow::MakeFrom(EGLNativeWindowType nativeWindow,
                                               EGLContext sharedContext) {
  if (!nativeWindow) {
    return nullptr;
  }
  auto device = EGLDevice::MakeFrom(nativeWindow, sharedContext);
  if (device == nullptr) {
    return nullptr;
  }
  auto eglWindow = std::shared_ptr<EGLWindow>(new EGLWindow(device));
  eglWindow->nativeWindow = nativeWindow;
  return eglWindow;
}

EGLWindow::EGLWindow(std::shared_ptr<Device> device) : Window(std::move(device)) {
}

std::shared_ptr<Surface> EGLWindow::onCreateSurface(Context* context) {
  EGLint width = 0;
  EGLint height = 0;

  if (nativeWindow) {
    // If the rendering size changes，eglQuerySurface() may give the wrong size on same platforms.
#if defined(__ANDROID__) || defined(ANDROID)
    width = ANativeWindow_getWidth(nativeWindow);
    height = ANativeWindow_getHeight(nativeWindow);
#elif defined(_WIN32)
    RECT rect;
    GetClientRect(nativeWindow, &rect);
    width = static_cast<int>(rect.right - rect.left);
    height = static_cast<int>(rect.bottom - rect.top);
#endif
  }

  if (width <= 0 || height <= 0) {
    auto eglDevice = static_cast<EGLDevice*>(device.get());
    eglQuerySurface(eglDevice->eglDisplay, eglDevice->eglSurface, EGL_WIDTH, &width);
    eglQuerySurface(eglDevice->eglDisplay, eglDevice->eglSurface, EGL_HEIGHT, &height);
  }

  if (width <= 0 || height <= 0) {
    return nullptr;
  }

  GLFrameBufferInfo frameBuffer = {};
  frameBuffer.id = 0;
  frameBuffer.format = GL_RGBA8;
  BackendRenderTarget renderTarget = {frameBuffer, width, height};
  return Surface::MakeFrom(context, renderTarget, ImageOrigin::BottomLeft);
}

void EGLWindow::onPresent(Context*, int64_t presentationTime) {
  auto device = std::static_pointer_cast<EGLDevice>(this->device);
  auto eglDisplay = device->eglDisplay;
  // eglSurface cannot be nullptr in EGLWindow.
  auto eglSurface = device->eglSurface;
  if (presentationTime != INT64_MIN) {
    static auto eglPresentationTimeANDROID = reinterpret_cast<PFNEGLPRESENTATIONTIMEANDROIDPROC>(
        eglGetProcAddress("eglPresentationTimeANDROID"));
    if (eglPresentationTimeANDROID) {
      // egl uses nano seconds
      eglPresentationTimeANDROID(eglDisplay, eglSurface, presentationTime * 1000);
    }
  }
  eglSwapBuffers(eglDisplay, eglSurface);
}
}  // namespace tgfx