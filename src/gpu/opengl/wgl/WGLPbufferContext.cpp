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

#include "WGLPbufferContext.h"
#include <mutex>
#include "core/utils/Log.h"

namespace tgfx {

static HWND CreateParentWindow() {
  static ATOM windowClass = 0;
  const auto hInstance = (HINSTANCE)GetModuleHandle(nullptr);
  if (!windowClass) {
    WNDCLASS wc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hbrBackground = nullptr;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hInstance = hInstance;
    wc.lpfnWndProc = static_cast<WNDPROC>(DefWindowProc);
    wc.lpszClassName = TEXT("WC_TGFX");
    wc.lpszMenuName = nullptr;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;

    windowClass = RegisterClass(&wc);
    if (!windowClass) {
      LOGE("CreateParentWindow() register window class failed.");
      return nullptr;
    }
  }

  HWND window = CreateWindow(TEXT("WC_TGFX"), TEXT("INVISIBLE WINDOW"), WS_OVERLAPPEDWINDOW, 0, 0,
                             1, 1, nullptr, nullptr, hInstance, nullptr);
  if (window == nullptr) {
    LOGE("CreateParentWindow() create window failed.");
    return nullptr;
  }

  return window;
}

bool CreatePbufferContext(HDC parentDC, HGLRC sharedContext, const WGLExtensions& extensions,
                          HPBUFFER& pBuffer, HDC& deviceContext, HGLRC& glContext) {
  if (!extensions.hasExtension(parentDC, "WGL_ARB_pixel_format") ||
      !extensions.hasExtension(parentDC, "WGL_ARB_pbuffer")) {
    return false;
  }

  static int pixelFormat = -1;
  static std::once_flag flag;
  std::call_once(flag, [parentDC, &extensions] {
    int pixelFormatsToTry[2] = {-1, -1};
    GetPixelFormatsToTry(parentDC, extensions, pixelFormatsToTry);
    pixelFormat = pixelFormatsToTry[0];
  });

  if (pixelFormat == -1) {
    return false;
  }

  pBuffer = extensions.createPbuffer(parentDC, pixelFormat, 1, 1, nullptr);
  if (pBuffer != nullptr) {
    deviceContext = extensions.getPbufferDC(pBuffer);
    if (deviceContext != nullptr) {
      glContext = CreateGLContext(deviceContext, extensions, sharedContext);
      if (glContext != nullptr) {
        return true;
      }
      extensions.releasePbufferDC(pBuffer, deviceContext);
    }
    extensions.destroyPbuffer(pBuffer);
  }

  return false;
}

WGLPbufferContext::WGLPbufferContext(HGLRC sharedContext) : WGLContext(sharedContext) {
  initializeContext();
}

WGLPbufferContext::~WGLPbufferContext() {
  destroyContext();
}

void WGLPbufferContext::onInitializeContext() {
  HWND window = CreateParentWindow();
  if (window == nullptr) {
    LOGE("WGLPbufferContext::onInitializeContext() create window failed!");
    return;
  }
  HDC parentDeviceContext = GetDC(window);
  if (parentDeviceContext == nullptr) {
    LOGE("WGLPbufferContext::onInitializeContext() get deivce context failed!");
    DestroyWindow((window));
    return;
  }
  bool result = CreatePbufferContext(parentDeviceContext, sharedContext, extensions, pBuffer,
                                     deviceContext, glContext);
  if (!result) {
    LOGE("WGLPbufferContext::onInitializeContext() create pbuffer context failed!");
  }
  ReleaseDC(window, parentDeviceContext);
  DestroyWindow(window);
}

void WGLPbufferContext::onDestroyContext() {
  if (pBuffer == nullptr) {
    return;
  }
  if (glContext != nullptr) {
    wglDeleteContext(glContext);
    glContext = nullptr;
  }
  if (deviceContext != nullptr) {
    extensions.releasePbufferDC(pBuffer, deviceContext);
    extensions.destroyPbuffer(pBuffer);
    deviceContext = nullptr;
  }
  pBuffer = nullptr;
}

}  // namespace tgfx