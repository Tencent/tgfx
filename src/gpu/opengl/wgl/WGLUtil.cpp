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

#include <vector>
#include "WGLInterface.h"
#include "core/utils/Log.h"

namespace tgfx {
void GetPixelFormatsToTry(HDC deviceContext, int formatsToTry[2]) {
  auto wglInterface = WGLInterface::Get();
  if (!wglInterface->pixelFormatSupport) {
    return;
  }
  std::vector<int> intAttributes{WGL_DRAW_TO_WINDOW,
                                 TRUE,
                                 WGL_DOUBLE_BUFFER,
                                 TRUE,
                                 WGL_ACCELERATION,
                                 WGL_FULL_ACCELERATION,
                                 WGL_SUPPORT_OPENGL,
                                 TRUE,
                                 WGL_COLOR_BITS,
                                 24,
                                 WGL_ALPHA_BITS,
                                 8,
                                 WGL_STENCIL_BITS,
                                 8,
                                 0,
                                 0};

  auto format = formatsToTry[0] ? &formatsToTry[0] : &formatsToTry[1];
  unsigned numFormats = 0;
  constexpr float floatAttributes[] = {0, 0};
  wglInterface->wglChoosePixelFormat(deviceContext, intAttributes.data(), floatAttributes, 1,
                                     format, &numFormats);
}

HGLRC CreateGLContext(HDC deviceContext, HGLRC sharedContext, bool vSyncEnabled) {
  auto oldDeviceContext = wglGetCurrentDC();
  auto oldGLContext = wglGetCurrentContext();

  HGLRC glContext = nullptr;
  auto wglInterface = WGLInterface::Get();
  if (wglInterface->createContextAttribsSupport) {
    const int coreProfileAttribs[] = {
        WGL_CONTEXT_MAJOR_VERSION,
        wglInterface->glMajorMax,
        WGL_CONTEXT_MINOR_VERSION,
        wglInterface->glMinorMax,
        WGL_CONTEXT_PROFILE_MASK,
        WGL_CONTEXT_CORE_PROFILE_BIT,
        0,
    };
    glContext =
        wglInterface->wglCreateContextAttribs(deviceContext, sharedContext, coreProfileAttribs);
  }

  if (glContext == nullptr) {
    glContext = wglCreateContext(deviceContext);
    if (glContext == nullptr) {
      LOGE("CreateGLContext() wglCreateContext failed.");
      return nullptr;
    }

    if (sharedContext != nullptr && !wglShareLists(sharedContext, glContext)) {
      wglDeleteContext(glContext);
      return nullptr;
    }
  }

  if (wglMakeCurrent(deviceContext, glContext) && wglInterface->swapIntervalSupport) {
    wglInterface->wglSwapInterval(vSyncEnabled ? 1 : 0);
  }

  wglMakeCurrent(oldDeviceContext, oldGLContext);
  return glContext;
}
}  // namespace tgfx
