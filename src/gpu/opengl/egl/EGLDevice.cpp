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

#include "tgfx/gpu/opengl/egl/EGLDevice.h"
#include "core/utils/Log.h"
#include "gpu/opengl/egl/EGLProcGetter.h"
#include "tgfx/gpu/opengl/egl/EGLGlobals.h"

namespace tgfx {
static EGLContext CreateContext(EGLContext sharedContext, EGLDisplay eglDisplay,
                                EGLConfig eglConfig) {
  static const EGLint context3Attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  auto eglContext = eglCreateContext(eglDisplay, eglConfig, sharedContext, context3Attributes);
  if (eglContext != EGL_NO_CONTEXT) {
    return eglContext;
  }
  LOGE("EGLDevice CreateContext() version 3: error=%d", eglGetError());
  static const EGLint context2Attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  eglContext = eglCreateContext(eglDisplay, eglConfig, sharedContext, context2Attributes);
  if (eglContext == EGL_NO_CONTEXT) {
    LOGE("EGLDevice CreateContext() version 2: error=%d", eglGetError());
  }
  return eglContext;
}

#if defined(_WIN32)
// Create a fixed-size window surface for ANGLE, as ANGLE can only detect resizes during a swap.
// https://groups.google.com/g/angleproject/c/j3SF7nVIpD8
static EGLSurface CreateFixedSizeSurfaceForAngle(EGLNativeWindowType nativeWindow,
                                                 const EGLGlobals* eglGlobals) {
  if (nativeWindow == nullptr) {
    return nullptr;
  }
  RECT rect;
  GetClientRect(nativeWindow, &rect);
  auto width = static_cast<int>(rect.right - rect.left);
  auto height = static_cast<int>(rect.bottom - rect.top);
  std::vector<EGLint> attributes = {
      EGL_FIXED_SIZE_ANGLE, EGL_TRUE, EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE,
  };
  attributes.insert(attributes.end(), eglGlobals->windowSurfaceAttributes.begin(),
                    eglGlobals->windowSurfaceAttributes.end());
  return eglCreateWindowSurface(eglGlobals->display, eglGlobals->windowConfig, nativeWindow,
                                attributes.data());
}
#endif

void* GLDevice::CurrentNativeHandle() {
  return eglGetCurrentContext();
}

std::shared_ptr<GLDevice> GLDevice::Current() {
  auto eglContext = eglGetCurrentContext();
  auto eglDisplay = eglGetCurrentDisplay();
  auto eglSurface = eglGetCurrentSurface(EGL_DRAW);
  return EGLDevice::Wrap(eglDisplay, eglSurface, eglContext, nullptr, true);
}

std::shared_ptr<GLDevice> GLDevice::Make(void* sharedContext) {
  auto eglGlobals = EGLGlobals::Get();
  auto eglSurface = eglCreatePbufferSurface(eglGlobals->display, eglGlobals->pbufferConfig,
                                            eglGlobals->pbufferSurfaceAttributes.data());
  if (eglSurface == nullptr) {
    LOGE("GLDevice::Make() eglCreatePbufferSurface error=%d", eglGetError());
    return nullptr;
  }
  auto eglShareContext = reinterpret_cast<EGLContext>(sharedContext);
  auto eglContext = CreateContext(eglShareContext, eglGlobals->display, eglGlobals->pbufferConfig);
  if (eglContext == nullptr) {
    eglDestroySurface(eglGlobals->display, eglSurface);
    return nullptr;
  }
  auto device =
      EGLDevice::Wrap(eglGlobals->display, eglSurface, eglContext, eglShareContext, false);
  if (device == nullptr) {
    eglDestroyContext(eglGlobals->display, eglContext);
    eglDestroySurface(eglGlobals->display, eglSurface);
  }
  return device;
}

std::shared_ptr<EGLDevice> EGLDevice::MakeFrom(EGLDisplay eglDisplay, EGLSurface eglSurface,
                                               EGLContext eglContext, bool adopted) {
  return EGLDevice::Wrap(eglDisplay, eglSurface, eglContext, nullptr, !adopted);
}

std::shared_ptr<EGLDevice> EGLDevice::MakeFrom(EGLNativeWindowType nativeWindow,
                                               EGLContext sharedContext) {
  auto eglGlobals = EGLGlobals::Get();
#if defined(_WIN32)
  auto eglSurface = CreateFixedSizeSurfaceForAngle(nativeWindow, eglGlobals);
#else
  auto eglSurface =
      eglCreateWindowSurface(eglGlobals->display, eglGlobals->windowConfig, nativeWindow,
                             eglGlobals->windowSurfaceAttributes.data());
#endif
  if (eglSurface == nullptr) {
    LOGE("EGLDevice::MakeFrom() eglCreateWindowSurface error=%d", eglGetError());
    return nullptr;
  }
  auto eglContext = CreateContext(sharedContext, eglGlobals->display, eglGlobals->windowConfig);
  if (eglContext == EGL_NO_CONTEXT) {
    eglDestroySurface(eglGlobals->display, eglSurface);
    return nullptr;
  }
  auto device = EGLDevice::Wrap(eglGlobals->display, eglSurface, eglContext, sharedContext, false);
  if (device == nullptr) {
    eglDestroyContext(eglGlobals->display, eglContext);
    eglDestroySurface(eglGlobals->display, eglSurface);
  }
  return device;
}

std::shared_ptr<EGLDevice> EGLDevice::Wrap(EGLDisplay eglDisplay, EGLSurface eglSurface,
                                           EGLContext eglContext, EGLContext shareContext,
                                           bool externallyOwned) {
  auto glContext = GLDevice::Get(eglContext);
  if (glContext) {
    return std::static_pointer_cast<EGLDevice>(glContext);
  }
  if (eglDisplay == nullptr || eglContext == nullptr) {
    // eglSurface could be nullptr when there is 'EGL_KHR_surfaceless_context' support.
    return nullptr;
  }
  EGLContext oldEglContext = eglGetCurrentContext();
  EGLDisplay oldEglDisplay = eglGetCurrentDisplay();
  EGLSurface oldEglReadSurface = eglGetCurrentSurface(EGL_READ);
  EGLSurface oldEglDrawSurface = eglGetCurrentSurface(EGL_DRAW);
  if (oldEglContext != eglContext) {
    auto result = eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
    if (!result) {
      eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      if (oldEglDisplay) {
        eglMakeCurrent(oldEglDisplay, oldEglDrawSurface, oldEglReadSurface, oldEglContext);
      }
      return nullptr;
    }
  }
  auto device = std::shared_ptr<EGLDevice>(new EGLDevice(eglContext));
  device->externallyOwned = externallyOwned;
  device->eglDisplay = eglDisplay;
  device->eglSurface = eglSurface;
  device->eglContext = eglContext;
  device->shareContext = shareContext;
  device->weakThis = device;
  if (oldEglContext != eglContext) {
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (oldEglDisplay) {
      eglMakeCurrent(oldEglDisplay, oldEglDrawSurface, oldEglReadSurface, oldEglContext);
    }
  }
  return device;
}

EGLDevice::EGLDevice(void* nativeHandle) : GLDevice(nativeHandle) {
}

EGLDevice::~EGLDevice() {
  releaseAll();
  if (externallyOwned) {
    return;
  }
  eglDestroyContext(eglDisplay, eglContext);
  if (eglSurface != nullptr) {
    eglDestroySurface(eglDisplay, eglSurface);
  }
}

bool EGLDevice::sharableWith(void* nativeContext) const {
  return nativeHandle == nativeContext || shareContext == nativeContext;
}

bool EGLDevice::onMakeCurrent() {
  oldEglContext = eglGetCurrentContext();
  oldEglDisplay = eglGetCurrentDisplay();
  oldEglReadSurface = eglGetCurrentSurface(EGL_READ);
  oldEglDrawSurface = eglGetCurrentSurface(EGL_DRAW);
  if (oldEglContext == eglContext) {
    // If the current context is already set by external, we don't need to switch it again.
    // The read/draw surface may be different.
    return true;
  }
#if defined(_WIN32)
  if (sizeInvalidWindow != nullptr) {
    eglDestroySurface(eglDisplay, eglSurface);
    eglSurface = CreateFixedSizeSurfaceForAngle(sizeInvalidWindow, EGLGlobals::Get());
    if (eglSurface == nullptr) {
      LOGE("EGLDevice::onMakeCurrent() CreateFixedSizeSurfaceForAngle error=%d", eglGetError());
      return false;
    }
    sizeInvalidWindow = nullptr;
  }
#endif
  auto result = eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
  if (!result) {
    LOGE("EGLDevice::onMakeCurrent() failure result = %d error= %d", result, eglGetError());
    return false;
  }
  return true;
}

void EGLDevice::onClearCurrent() {
  if (oldEglContext == eglContext) {
    return;
  }
  eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (oldEglDisplay) {
    // 可能失败。
    eglMakeCurrent(oldEglDisplay, oldEglDrawSurface, oldEglReadSurface, oldEglContext);
  }
}
}  // namespace tgfx