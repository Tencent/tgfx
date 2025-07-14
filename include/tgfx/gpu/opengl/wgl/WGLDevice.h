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

#pragma once

#include <windows.h>
#include "tgfx/gpu/opengl/GLDevice.h"

namespace tgfx {
class WGLDevice : public GLDevice {
 public:
  /**
   * Creates a WGLDevice with the existing HWND and HGLRC
   */
  static std::shared_ptr<WGLDevice> MakeFrom(HWND nativeWindow, HGLRC sharedContext);

  bool sharableWith(void* nativeConext) const override;

 protected:
  bool onMakeCurrent() override;
  void onClearCurrent() override;

 protected:
  HDC deviceContext = nullptr;
  HGLRC glContext = nullptr;
  HGLRC sharedContext = nullptr;

  HDC oldDeviceContext = nullptr;
  HGLRC oldGLContext = nullptr;

  explicit WGLDevice(HGLRC nativeHandle);

  static std::shared_ptr<WGLDevice> Wrap(HWND nativeWindow, HDC deviceContext, HGLRC glContext,
                                         HGLRC sharedContext, bool externallyOwned);

  friend class GLDevice;
  friend class WGLWindow;
};
}  // namespace tgfx
