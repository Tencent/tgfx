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

#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <atomic>
#include "tgfx/gpu/opengl/GLDevice.h"

#ifdef None
#undef None
#endif
#ifdef Always
#undef Always
#endif

namespace tgfx {
class ColorSpace;
class EGLDevice : public GLDevice {
 public:
  /**
   * Creates an EGLDevice with the existing EGLDisplay, EGLSurface, and EGLContext. If adopted is
   * true, the EGLDevice will take ownership of the EGLDisplay, EGLSurface, and EGLContext and
   * destroy them when the EGLDevice is destroyed. The caller should not destroy the EGLDisplay,
   * EGLSurface, and EGLContext in this case.
   */
  static std::shared_ptr<EGLDevice> MakeFrom(EGLDisplay eglDisplay, EGLSurface eglSurface,
                                             EGLContext eglContext, bool adopted = false);

  ~EGLDevice() override;

  bool sharableWith(void* nativeHandle) const override;

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  EGLDisplay eglDisplay = nullptr;
  EGLSurface eglSurface = nullptr;
  EGLContext eglContext = nullptr;
  EGLContext shareContext = nullptr;
  std::atomic<EGLNativeWindowType> sizeInvalidWindow = {EGLNativeWindowType(0)};

  EGLDisplay oldEglDisplay = nullptr;
  EGLContext oldEglContext = nullptr;
  EGLSurface oldEglReadSurface = nullptr;
  EGLSurface oldEglDrawSurface = nullptr;

  static std::shared_ptr<EGLDevice> MakeFrom(EGLNativeWindowType nativeWindow,
                                             EGLContext sharedContext = nullptr, std::shared_ptr<ColorSpace> colorSpace = nullptr);

  static std::shared_ptr<EGLDevice> Wrap(EGLDisplay eglDisplay, EGLSurface eglSurface,
                                         EGLContext eglContext, EGLContext shareContext,
                                         bool externallyOwned);

  EGLDevice(std::unique_ptr<GPU> gpu, void* nativeHandle);

  friend class GLDevice;

  friend class EGLWindow;
};
}  // namespace tgfx
