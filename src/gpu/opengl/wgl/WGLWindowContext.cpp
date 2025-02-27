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

#include "WGLWindowContext.h"
#include "core/utils/Log.h"

namespace tgfx {
HGLRC CreateWGLContext(HDC deviceContext, HGLRC sharedContext, const WGLExtensions& extensions) {
  if (!extensions.hasExtension(deviceContext, "WGL_ARB_pixel_format")) {
    return nullptr;
  }
  bool set = false;
  int pixelFormatsToTry[2] = {-1, -1};
  GetPixelFormatsToTry(deviceContext, extensions, pixelFormatsToTry);
  for (auto f = 0; !set && pixelFormatsToTry[f] && f < 2; ++f) {
    PIXELFORMATDESCRIPTOR descriptor;
    DescribePixelFormat(deviceContext, pixelFormatsToTry[f], sizeof(descriptor), &descriptor);
    set = SetPixelFormat(deviceContext, pixelFormatsToTry[f], &descriptor);
  }

  if (!set) {
    return nullptr;
  }

  return CreateGLContext(deviceContext, extensions, sharedContext);
}

WGLWindowContext::WGLWindowContext(HWND hWnd, HGLRC sharedContext)
    : WGLContext(sharedContext), hWnd(hWnd) {
  initializeContext();
}

WGLWindowContext::~WGLWindowContext() {
  destroyContext();
}

void WGLWindowContext::onInitializeContext() {
  if (hWnd != nullptr) {
    deviceContext = GetDC(hWnd);
    glContext = CreateWGLContext(deviceContext, sharedContext, extensions);
  } else {
    deviceContext = wglGetCurrentDC();
    glContext = wglGetCurrentContext();
  }

  if (deviceContext == nullptr || glContext == nullptr) {
    LOGE("WGLWindowContext::onInitializeContext() initializeWGLContext failed!");
  }
}

void WGLWindowContext::onDestroyContext() {
  if (hWnd == nullptr) {
    return;
  }

  if (glContext != nullptr) {
    wglDeleteContext(glContext);
    glContext = nullptr;
  }

  if (deviceContext != nullptr) {
    ReleaseDC(hWnd, deviceContext);
    deviceContext = nullptr;
  }
  hWnd = nullptr;
}

}  // namespace tgfx