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
#include "gpu/opengl/GLDefines.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
std::shared_ptr<EGLWindow> EGLWindow::Current() {
  auto device = std::static_pointer_cast<EGLDevice>(GLDevice::Current());
  if (device == nullptr || device->eglSurface == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<EGLWindow>(new EGLWindow(device, device->colorSpace));
}

std::shared_ptr<EGLWindow> EGLWindow::MakeFrom(EGLNativeWindowType nativeWindow,
                                               EGLContext sharedContext,
                                               std::shared_ptr<ColorSpace> colorSpace,
                                               int sampleCount) {
  if (!nativeWindow) {
    return nullptr;
  }
  auto device = EGLDevice::MakeFrom(nativeWindow, sharedContext, colorSpace, sampleCount);
  if (device == nullptr) {
    return nullptr;
  }
  auto eglWindow =
      std::shared_ptr<EGLWindow>(new EGLWindow(device, std::move(colorSpace), sampleCount));
  eglWindow->nativeWindow = nativeWindow;
  return eglWindow;
}

EGLWindow::EGLWindow(std::shared_ptr<Device> device, std::shared_ptr<ColorSpace> colorSpace,
                     int sampleCount)
    : Window(std::move(device), std::move(colorSpace), sampleCount) {
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

std::shared_ptr<RenderTargetProxy> EGLWindow::onCreateRenderTarget(Context* context) {
  auto eglDevice = static_cast<EGLDevice*>(device.get());
  if (nativeWindow) {
    if (!eglDevice->recreateSurfaceIfNeeded(nativeWindow)) {
      return nullptr;
    }
  }
  ISize size = {0, 0};
  if (nativeWindow) {
    // If the rendering size changes，eglQuerySurface() may give the wrong size on same platforms.
    size = GetNativeWindowSize(nativeWindow);
  }
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
  auto sampleCount = _sampleCount;
  if (!nativeWindow) {
    // For the Current() path, query the actual MSAA sample count from the GL context since there is
    // no user-specified sampleCount parameter.
    int samples = 1;
    static_cast<GLGPU*>(context->gpu())->functions()->getIntegerv(GL_SAMPLES, &samples);
    sampleCount = std::max(samples, 1);
  }
  BackendRenderTarget renderTarget = {frameBuffer, size.width, size.height, sampleCount};
  return RenderTargetProxy::MakeFrom(context, renderTarget, ImageOrigin::BottomLeft);
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
