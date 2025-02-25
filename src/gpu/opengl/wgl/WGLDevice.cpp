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

#include "tgfx/gpu/opengl/wgl/WGLDevice.h"
#include <GL/GL.h>
#include "WGL/wgl.h"
#include "core/utils/Log.h"
#include "tgfx/gpu/opengl/wgl/WGLExtensions.h"

namespace tgfx {
static void GetPixelFormatsToTry(HDC dc, const WGLExtensions& extensions, int formatsToTry[2]) {
  std::vector<int> iAttrs{WGL_DRAW_TO_WINDOW_ARB,
                          TRUE,
                          WGL_DOUBLE_BUFFER_ARB,
                          TRUE,
                          WGL_ACCELERATION_ARB,
                          WGL_FULL_ACCELERATION_ARB,
                          WGL_SUPPORT_OPENGL_ARB,
                          TRUE,
                          WGL_COLOR_BITS_ARB,
                          24,
                          WGL_ALPHA_BITS_ARB,
                          8,
                          WGL_STENCIL_BITS_ARB,
                          8,
                          0,
                          0};

  int* format = formatsToTry[0] ? &formatsToTry[0] : &formatsToTry[1];
  unsigned num;
  constexpr float fAttrs[] = {0, 0};
  extensions.choosePixelFormat(dc, iAttrs.data(), fAttrs, 1, format, &num);
}

static HGLRC CreateGLContext(HDC dc, const WGLExtensions& extensions, HGLRC sharedContext) {
  HDC oldDC = wglGetCurrentDC();
  HGLRC oldGLRC = wglGetCurrentContext();

  HGLRC glrc = wglCreateContext(dc);
  DEBUG_ASSERT(glrc);
  if (sharedContext != nullptr && !wglShareLists(sharedContext, glrc)) {
    wglDeleteContext(glrc);
    return nullptr;
  }

  wglMakeCurrent(oldDC, oldGLRC);
  return glrc;
}

HGLRC CreateWGLContext(HDC dc, HGLRC sharedContext) {
  WGLExtensions extensions;
  if (!extensions.hasExtension(dc, "WGL_ARB_pixel_format")) {
    return nullptr;
  }
  bool set = false;
  int pixelFormatsToTry[2] = {-1, -1};
  GetPixelFormatsToTry(dc, extensions, pixelFormatsToTry);
  for (auto f = 0; !set && pixelFormatsToTry[f] && f < 2; ++f) {
    PIXELFORMATDESCRIPTOR pfd;
    DescribePixelFormat(dc, pixelFormatsToTry[f], sizeof(pfd), &pfd);
    set = SetPixelFormat(dc, pixelFormatsToTry[f], &pfd);
  }

  if (!set) {
    return nullptr;
  }

  return CreateGLContext(dc, extensions, sharedContext);
}

bool CreatePbufferContext(HDC parentDC, HGLRC sharedContext, HDC& dc, HGLRC& glrc) {
  WGLExtensions extensions;
  if (!extensions.hasExtension(parentDC, "WGL_ARB_pixel_format") ||
      !extensions.hasExtension(parentDC, "WGL_ARB_pbuffer")) {
    return false;
  }

  static int gPixelFormat = -1;
  static std::once_flag flag;
  std::call_once(flag, [parentDC, &extensions] {
    int pixelFormatsToTry[2] = {-1, -1};
    GetPixelFormatsToTry(parentDC, extensions, pixelFormatsToTry);
    gPixelFormat = pixelFormatsToTry[0];
  });

  if (gPixelFormat == -1) {
    return false;
  }

  auto pbuf = extensions.createPbuffer(parentDC, gPixelFormat, 1, 1, nullptr);
  if (pbuf != nullptr) {
    dc = extensions.getPbufferDC(pbuf);
    if (dc != nullptr) {
      glrc = CreateGLContext(dc, extensions, sharedContext);
      if (glrc != nullptr) {
        return true;
      }
      extensions.releasePbufferDC(pbuf, dc);
    }
    extensions.destroyPbuffer(pbuf);
  }

  return false;
}

std::shared_ptr<GLDevice> GLDevice::Current() {
  auto context = wglGetCurrentContext();
  auto glDevice = GLDevice::Get(context);
  if (glDevice != nullptr) {
    return std::static_pointer_cast<WGLDevice>(glDevice);
  }
  auto dc = wglGetCurrentDC();
  return WGLDevice::Wrap(dc, context, nullptr, true);
}

std::shared_ptr<GLDevice> GLDevice::Make(void* sharedContext) {
  static ATOM gWC = 0;
  const auto hInstance = (HINSTANCE)GetModuleHandle(nullptr);
  if (!gWC) {
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

    gWC = RegisterClass(&wc);
    if (!gWC) {
      LOGE("GLDevice::Make() register window class failed.");
      return nullptr;
    }
  }

  HWND window = CreateWindow(TEXT("WC_TGFX"), TEXT("INVISIBLE"), WS_OVERLAPPEDWINDOW, 0, 0, 1, 1,
                             nullptr, nullptr, hInstance, nullptr);
  if (window == nullptr) {
    LOGE("GLDevice::Make() create window failed.");
    return nullptr;
  }

  HDC parentDC = GetDC(window);
  if (parentDC == nullptr) {
    LOGE("GLDevice::Make() get device context failed.");
    DestroyWindow(window);
    return nullptr;
  }

  HDC dc{nullptr};
  HGLRC glrc{nullptr};

  const bool result = CreatePbufferContext(parentDC, static_cast<HGLRC>(sharedContext), dc, glrc);
  ReleaseDC(window, parentDC);
  DestroyWindow(window);
  if (!result) {
    LOGE("GLDevice::Make() create pbuffer context failed.");
    return nullptr;
  }
  return WGLDevice::Wrap(dc, glrc, static_cast<HGLRC>(sharedContext), false);
}

std::shared_ptr<WGLDevice> WGLDevice::MakeFrom(HWND hWnd, HGLRC sharedContext) {
  if (hWnd == nullptr) {
    return nullptr;
  }

  auto dc = GetDC(hWnd);
  auto glrc = CreateWGLContext(dc, sharedContext);
  if (glrc == nullptr) {
    LOGE("WGLDevice::MakeFrom() CreateWGLContext failed!");
    return nullptr;
  }
  return WGLDevice::Wrap(dc, glrc, sharedContext, false);
}

std::shared_ptr<WGLDevice> WGLDevice::Wrap(HDC dc, HGLRC context, HGLRC sharedContext,
                                           bool externallyOwned) {
  auto glDevice = GLDevice::Get(context);
  if (glDevice != nullptr) {
    return std::static_pointer_cast<WGLDevice>(glDevice);
  }
  if (context == nullptr) {
    return nullptr;
  }

  auto oldDc = wglGetCurrentDC();
  auto oldContext = wglGetCurrentContext();
  if (oldContext != context) {
    auto result = wglMakeCurrent(dc, context);
    if (!result) {
      return nullptr;
    }
  }
  auto device = std::shared_ptr<WGLDevice>(new WGLDevice(context));
  device->externallyOwned = externallyOwned;
  device->dc = dc;
  device->glContext = context;
  device->sharedContext = sharedContext;
  device->weakThis = device;
  if (oldContext != context) {
    wglMakeCurrent(oldDc, oldContext);
  }
  return device;
}

WGLDevice::WGLDevice(HGLRC nativeHandle) : GLDevice(nativeHandle) {
}

WGLDevice::~WGLDevice() {
  releaseAll();
  if (externallyOwned) {
    return;
  }
  wglMakeCurrent(dc, nullptr);
  wglDeleteContext(glContext);
}

bool WGLDevice::sharableWith(void* nativeConext) const {
  return nativeHandle == nativeConext || sharedContext == nativeConext;
}

bool WGLDevice::onMakeCurrent() {
  oldContext = wglGetCurrentContext();
  oldDc = wglGetCurrentDC();
  if (oldContext == glContext) {
    return true;
  }
  if (!wglMakeCurrent(dc, glContext)) {
    return false;
  }
  return true;
}

void WGLDevice::onClearCurrent() {
  if (oldContext == glContext) {
    return;
  }
  wglMakeCurrent(dc, nullptr);
  if (oldDc != nullptr) {
    wglMakeCurrent(oldDc, oldContext);
  }
}
}  // namespace tgfx