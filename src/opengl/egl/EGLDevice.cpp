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

#include "tgfx/opengl/egl/EGLDevice.h"
#include <tgfx/opengl/egl/EGLGlobals.h>
#include "tgfx/opengl/GLDefines.h"
#include "opengl/GLInterface.h"
#include "opengl/egl/EGLProcGetter.h"
#include "utils/Log.h"

#ifndef EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT 0x3138
#endif

#ifndef EGL_LOSE_CONTEXT_ON_RESET_EXT
#define EGL_LOSE_CONTEXT_ON_RESET_EXT 0x31BF
#endif

namespace tgfx {
static EGLContext CreateContext(EGLContext sharedContext, EGLDisplay eglDisplay,
                                EGLConfig eglConfig, bool contextRobustnessSupported) {
  // Try ES 3 with robustness if supported.
  if (contextRobustnessSupported) {
    static const EGLint robust3Attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3,
                                               EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT,
                                               EGL_LOSE_CONTEXT_ON_RESET_EXT, EGL_NONE};
    auto eglContext = eglCreateContext(eglDisplay, eglConfig, sharedContext, robust3Attributes);
    if (eglContext != EGL_NO_CONTEXT) {
      return eglContext;
    }
    // Try ES 2 with robustness.
    static const EGLint robust2Attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                               EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT,
                                               EGL_LOSE_CONTEXT_ON_RESET_EXT, EGL_NONE};
    eglContext = eglCreateContext(eglDisplay, eglConfig, sharedContext, robust2Attributes);
    if (eglContext != EGL_NO_CONTEXT) {
      return eglContext;
    }
    // Robustness creation failed, fall through to non-robust path.
  }
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
  static auto eglGlobals = EGLGlobals::Get();
  auto eglContext = EGL_NO_CONTEXT;
  auto eglSurface = eglCreatePbufferSurface(eglGlobals->display, eglGlobals->pbufferConfig,
                                            eglGlobals->pbufferSurfaceAttributes.data());
  if (eglSurface == nullptr) {
    LOGE("GLDevice::Make() eglCreatePbufferSurface error=%d", eglGetError());
    return nullptr;
  }
  auto eglShareContext = reinterpret_cast<EGLContext>(sharedContext);
  eglContext = CreateContext(eglShareContext, eglGlobals->display, eglGlobals->pbufferConfig,
                                  eglGlobals->contextRobustnessSupported);
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
  static auto eglGlobals = EGLGlobals::Get();
  auto eglSurface =
      eglCreateWindowSurface(eglGlobals->display, eglGlobals->windowConfig, nativeWindow,
                             eglGlobals->windowSurfaceAttributes.data());
  if (eglSurface == nullptr) {
    LOGE("EGLDevice::MakeFrom() eglCreateWindowSurface error=%d", eglGetError());
    return nullptr;
  }
  auto eglContext = CreateContext(sharedContext, eglGlobals->display, eglGlobals->windowConfig,
                                  eglGlobals->contextRobustnessSupported);
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
    return !checkGraphicsResetStatus();
  }
  auto result = eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
  if (!result) {
    auto error = eglGetError();
    if (error == EGL_CONTEXT_LOST) {
      LOGE("EGLDevice::onLockContext() EGL_CONTEXT_LOST detected.");
      handleContextLost();
    } else {
      LOGE("EGLDevice::onLockContext() failure result = %d error= %d", result, error);
    }
    return false;
  }
  if (checkGraphicsResetStatus()) {
    return false;
  }
  return true;
}

void EGLDevice::onClearCurrent() {
  if (oldEglContext == eglContext) {
    return;
  }
  auto result = eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (!result) {
    auto error = eglGetError();
    if (error == EGL_CONTEXT_LOST) {
      LOGE("EGLDevice::onUnlockContext() EGL_CONTEXT_LOST detected.");
      handleContextLost();
    } else {
      LOGE("EGLDevice::onUnlockContext() failure error=%d", error);
    }
  }
  if (oldEglDisplay) {
    // 可能失败。
    eglMakeCurrent(oldEglDisplay, oldEglDrawSurface, oldEglReadSurface, oldEglContext);
  }
}

void EGLDevice::handleContextLost() {
  if (_contextLost) {
    return;
  }
  LOGE("EGLDevice::handleContextLost() Context [%p] has been permanently lost.", eglContext);
  // On EGL platforms, a GPU reset affects all contexts. Notify all registered devices.
  MarkAllContextsLost();
}

bool EGLDevice::checkGraphicsResetStatus() {
  if (graphicsResetStatus != GL_NO_ERROR) {
    return true;
  }
  auto glInterface = GLInterface::GetNative();
  if (glInterface == nullptr) {
    return false;
  }
  auto getGraphicsResetStatus = glInterface->functions->getGraphicsResetStatus;
  if (getGraphicsResetStatus == nullptr) {
    return false;
  }
  auto status = getGraphicsResetStatus();
  if (status != GL_NO_ERROR) {
    graphicsResetStatus = status;
    LOGE("EGLDevice::checkGraphicsResetStatus() GPU reset detected: status=0x%x", status);
    handleContextLost();
    return true;
  }
  return false;
}
}  // namespace tgfx
