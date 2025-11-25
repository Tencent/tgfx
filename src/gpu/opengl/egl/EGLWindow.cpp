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

#include "tgfx/gpu/opengl/egl/EGLWindow.h"

#if defined(__ANDROID__) || defined(ANDROID)
#include <android/native_window.h>
#elif defined(__OHOS__)
#include <native_window/external_window.h>
#elif defined(_WIN32)
#include <WinUser.h>
#endif
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include "core/utils/USE.h"

#ifndef EGL_GL_COLORSPACE_DISPLAY_P3_PASSTHROUGH_EXT
#define EGL_GL_COLORSPACE_DISPLAY_P3_PASSTHROUGH_EXT -1
#endif

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

ISize GetNativeWindowSize(EGLNativeWindowType nativeWindow) {
  ISize size = {0, 0};
#if defined(__ANDROID__) || defined(ANDROID)
  size.width = ANativeWindow_getWidth(nativeWindow);
  size.height = ANativeWindow_getHeight(nativeWindow);
#elif defined(__OHOS__)
  OH_NativeWindow_NativeWindowHandleOpt(reinterpret_cast<OHNativeWindow*>(nativeWindow),
                                        GET_BUFFER_GEOMETRY, &size.height, &size.width);
#elif defined(_WIN32)
  RECT rect = {};
  GetClientRect(nativeWindow, &rect);
  size.width = static_cast<int>(rect.right - rect.left);
  size.height = static_cast<int>(rect.bottom - rect.top);
#else
  USE(nativeWindow);
#endif
  return size;
}

void EGLWindow::onInvalidSize() {
#if defined(_WIN32)
  if (nativeWindow == nullptr) {
    return;
  }
  auto size = GetNativeWindowSize(nativeWindow);
  EGLint surfaceWidth = 0;
  EGLint surfaceHeight = 0;
  auto eglDevice = static_cast<EGLDevice*>(device.get());
  eglQuerySurface(eglDevice->eglDisplay, eglDevice->eglSurface, EGL_WIDTH, &surfaceWidth);
  eglQuerySurface(eglDevice->eglDisplay, eglDevice->eglSurface, EGL_HEIGHT, &surfaceHeight);
  if (surfaceWidth != size.width || surfaceHeight != size.height) {
    eglDevice->sizeInvalidWindow.store(nativeWindow, std::memory_order_relaxed);
  }
#endif
}

std::shared_ptr<Surface> EGLWindow::onCreateSurface(Context* context,
                                                    std::shared_ptr<ColorSpace> colorSpace) {
  ISize size = {0, 0};
  if (nativeWindow) {
    // If the rendering size changes，eglQuerySurface() may give the wrong size on same platforms.
    size = GetNativeWindowSize(nativeWindow);
  }
  auto eglDevice = static_cast<EGLDevice*>(device.get());
  if (size.width <= 0 || size.height <= 0) {
    eglQuerySurface(eglDevice->eglDisplay, eglDevice->eglSurface, EGL_WIDTH, &size.width);
    eglQuerySurface(eglDevice->eglDisplay, eglDevice->eglSurface, EGL_HEIGHT, &size.height);
  }
  if (size.width <= 0 || size.height <= 0) {
    return nullptr;
  }
  GLFrameBufferInfo frameBuffer = {};
  frameBuffer.id = 0;
  frameBuffer.format = GL_RGBA8;
  BackendRenderTarget renderTarget = {frameBuffer, size.width, size.height};
  return Surface::MakeFrom(context, renderTarget, ImageOrigin::BottomLeft, 0,
                           std::move(colorSpace));
}

void EGLWindow::setPresentationTime(int64_t time) {
  presentationTime = time;
}

void EGLWindow::onPresent(Context*) {
  auto device = std::static_pointer_cast<EGLDevice>(this->device);
  auto eglDisplay = device->eglDisplay;
  // eglSurface cannot be nullptr in EGLWindow.
  auto eglSurface = device->eglSurface;
  if (presentationTime.has_value()) {
    static auto eglPresentationTimeANDROID = reinterpret_cast<PFNEGLPRESENTATIONTIMEANDROIDPROC>(
        eglGetProcAddress("eglPresentationTimeANDROID"));
    if (eglPresentationTimeANDROID) {
      // egl uses nano seconds
      eglPresentationTimeANDROID(eglDisplay, eglSurface, *presentationTime * 1000);
      presentationTime = std::nullopt;
    }
  }
  eglSwapBuffers(eglDisplay, eglSurface);
}
}  // namespace tgfx