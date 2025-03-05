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

#include "WGLUtil.h"
#include <GL/GL.h>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "core/utils/Log.h"

namespace tgfx {
#define TEMP_CLASS TEXT("TempClass")
static ATOM WindowClass = 0;
static HWND CreateTempWindow() {
  HINSTANCE instance = GetModuleHandle(nullptr);
  if (!WindowClass) {
    WNDCLASS wc;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = static_cast<WNDPROC>(DefWindowProc);
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = instance;
    wc.hIcon = LoadIcon(nullptr, IDI_WINLOGO);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = TEMP_CLASS;

    WindowClass = RegisterClass(&wc);
    if (!WindowClass) {
      LOGE("CreateTempWindow() register window class failed.");
      return nullptr;
    }
  }

  auto nativeWindow =
      CreateWindow(TEMP_CLASS, TEXT("PlaceholderWindow"), WS_POPUP | WS_CLIPCHILDREN, 0, 0, 8, 8,
                   nullptr, nullptr, instance, nullptr);
  if (nativeWindow == nullptr) {
    UnregisterClass(TEMP_CLASS, instance);
    return nullptr;
  }

  return nativeWindow;
}

#define GET_PROC(NAME, SUFFIX) wgl##NAME = (NAME##Proc)wglGetProcAddress("wgl" #NAME #SUFFIX)

#define WGL_DRAW_TO_WINDOW 0x2001
#define WGL_ACCELERATION 0x2003
#define WGL_SUPPORT_OPENGL 0x2010
#define WGL_DOUBLE_BUFFER 0x2011
#define WGL_COLOR_BITS 0x2014
#define WGL_ALPHA_BITS 0x201B
#define WGL_STENCIL_BITS 0x2023
#define WGL_FULL_ACCELERATION 0x2027

using GetExtensionsStringProc = const char*(WINAPI*)(HDC);
using ChoosePixelFormatProc = BOOL(WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
using SwapIntervalProc = BOOL(WINAPI*)(int);
using CreatePbufferProc = HPBUFFER(WINAPI*)(HDC, int, int, int, const int*);
using GetPbufferDCProc = HDC(WINAPI*)(HPBUFFER);
using ReleasePbufferDCProc = int(WINAPI*)(HPBUFFER, HDC);
using DestroyPbufferProc = BOOL(WINAPI*)(HPBUFFER);

static GetExtensionsStringProc wglGetExtensionsString = nullptr;
static ChoosePixelFormatProc wglChoosePixelFormat = nullptr;
static CreatePbufferProc wglCreatePbuffer = nullptr;
static GetPbufferDCProc wglGetPbufferDC = nullptr;
static ReleasePbufferDCProc wglReleasePbufferDC = nullptr;
static DestroyPbufferProc wglDestroyPbuffer = nullptr;

static bool Initialised = false;
static bool PixelFormtSupport = false;
static bool PBufferSupport = false;

static void InitialiseExtensions(HDC deviceContext) {
  if (deviceContext == nullptr || wglGetExtensionsString == nullptr) {
    LOGE("InitialiseExtensions() context is invalid");
    return;
  }
  const char* extensionString = wglGetExtensionsString(deviceContext);
  if (extensionString == nullptr) {
    LOGE("InitialiseExtensions() extentionString is nullptr");
    return;
  }
  std::stringstream extensionStream;
  std::string element;
  extensionStream << extensionString;
  std::set<std::string> extensionList;
  while (extensionStream >> element) {
    extensionList.insert(element);
  }
  PixelFormtSupport = extensionList.find("WGL_ARB_pixel_format") != extensionList.end();
  PBufferSupport = extensionList.find("WGL_ARB_pbuffer") != extensionList.end();
}

static void InitialiseWGL() {
  if (Initialised) {
    return;
  }
  auto oldDeviceContext = wglGetCurrentDC();
  auto oldGLContext = wglGetCurrentContext();

  PIXELFORMATDESCRIPTOR descriptor;
  ZeroMemory(&descriptor, sizeof(descriptor));
  descriptor.nSize = sizeof(descriptor);
  descriptor.nVersion = 1;
  descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
  descriptor.iPixelType = PFD_TYPE_RGBA;
  descriptor.cColorBits = 32;
  descriptor.cDepthBits = 0;
  descriptor.cStencilBits = 8;
  descriptor.iLayerType = PFD_MAIN_PLANE;

  HWND nativeWindow = CreateTempWindow();
  if (nativeWindow) {
    HDC deviceContet = GetDC(nativeWindow);
    int format = ChoosePixelFormat(deviceContet, &descriptor);
    SetPixelFormat(deviceContet, format, &descriptor);
    HGLRC glContext = wglCreateContext(deviceContet);
    DEBUG_ASSERT(glContext);
    wglMakeCurrent(deviceContet, glContext);
    GET_PROC(GetExtensionsString, ARB);
    GET_PROC(ChoosePixelFormat, ARB);
    GET_PROC(CreatePbuffer, ARB);
    GET_PROC(GetPbufferDC, ARB);
    GET_PROC(ReleasePbufferDC, ARB);
    GET_PROC(DestroyPbuffer, ARB);
    InitialiseExtensions(deviceContet);
    wglMakeCurrent(deviceContet, nullptr);
    wglDeleteContext(glContext);
    DestroyWindow(nativeWindow);
  }
  wglMakeCurrent(oldDeviceContext, oldGLContext);
  Initialised = true;
}

static void GetPixelFormatsToTry(HDC deviceContext, int formatsToTry[2]) {
  std::vector<int> iAttributes{WGL_DRAW_TO_WINDOW,
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
  unsigned numFormats;
  constexpr float fAttributes[] = {0, 0};
  wglChoosePixelFormat(deviceContext, iAttributes.data(), fAttributes, 1, format, &numFormats);
}

static HGLRC CreateGLContext(HDC deviceContext, HGLRC sharedContext) {
  auto oldDeviceContext = wglGetCurrentDC();
  auto oldGLContext = wglGetCurrentContext();
  auto glContext = wglCreateContext(deviceContext);
  if (glContext == nullptr) {
    LOGE("CreateGLContext() wglCreateContext failed.");
    return nullptr;
  }
  if (sharedContext != nullptr && !wglShareLists(sharedContext, glContext)) {
    wglDeleteContext(glContext);
    return nullptr;
  }
  wglMakeCurrent(oldDeviceContext, oldGLContext);
  return glContext;
}

bool CreateWGLContext(HWND nativeWindow, HGLRC sharedContext, HDC& deviceContext,
                      HGLRC& glContext) {
  InitialiseWGL();
  if (!PixelFormtSupport) {
    return false;
  }
  deviceContext = GetDC(nativeWindow);
  auto set = false;
  int pixelFormatsToTry[2] = {-1, -1};
  GetPixelFormatsToTry(deviceContext, pixelFormatsToTry);
  for (auto f = 0; !set && pixelFormatsToTry[f] && f < 2; ++f) {
    PIXELFORMATDESCRIPTOR descriptor;
    DescribePixelFormat(deviceContext, pixelFormatsToTry[f], sizeof(descriptor), &descriptor);
    set = SetPixelFormat(deviceContext, pixelFormatsToTry[f], &descriptor);
  }

  auto releaseDC = [&]() {
    ReleaseDC(nativeWindow, deviceContext);
    deviceContext = nullptr;
  };

  if (!set) {
    releaseDC();
    return nullptr;
  }
  glContext = CreateGLContext(deviceContext, sharedContext);
  if (glContext == nullptr) {
    releaseDC();
    return false;
  }
  return true;
}

bool CreatePbufferContext(HGLRC sharedContext, HPBUFFER& pBuffer, HDC& deviceContext,
                          HGLRC& glContext) {
  InitialiseWGL();
  if (!PBufferSupport || !PixelFormtSupport) {
    return false;
  }
  HWND window = nullptr;
  HDC parentDeviceContext = nullptr;
  auto result = false;
  do {
    window = CreateTempWindow();
    if (window == nullptr) {
      LOGE("CreatePbufferContext() create window failed!");
      break;
    }
    parentDeviceContext = GetDC(window);
    if (parentDeviceContext == nullptr) {
      LOGE("CreatePbufferContext() get deivce context failed!");
      break;
    }
    static auto pixelFormat = -1;
    static std::once_flag flag;
    std::call_once(flag, [parentDeviceContext] {
      int pixelFormatsToTry[2] = {-1, -1};
      GetPixelFormatsToTry(parentDeviceContext, pixelFormatsToTry);
      pixelFormat = pixelFormatsToTry[0];
    });
    if (pixelFormat == -1) {
      break;
    }
    pBuffer = wglCreatePbuffer(parentDeviceContext, pixelFormat, 1, 1, nullptr);
    if (pBuffer == nullptr) {
      LOGE("CreatePbufferContext() create pbuffer failed!");
      break;
    }
    deviceContext = wglGetPbufferDC(pBuffer);
    if (deviceContext != nullptr) {
      glContext = CreateGLContext(deviceContext, sharedContext);
      if (glContext != nullptr) {
        result = true;
        break;
      }
      wglReleasePbufferDC(pBuffer, deviceContext);
      deviceContext = nullptr;
    }
    wglDestroyPbuffer(pBuffer);
    pBuffer = nullptr;
  } while (false);

  ReleaseDC(window, parentDeviceContext);
  DestroyWindow(window);
  return result;
}

bool ReleasePbufferDC(HPBUFFER pBuffer, HDC deviceContext) {
  InitialiseWGL();
  if (!PBufferSupport) {
    return false;
  }
  if (pBuffer == nullptr) {
    return false;
  }
  return wglReleasePbufferDC(pBuffer, deviceContext) == 1;
}

bool DestroyPbuffer(HPBUFFER pBuffer) {
  InitialiseWGL();
  if (!PBufferSupport) {
    return false;
  }
  return wglDestroyPbuffer(pBuffer);
}
}  // namespace tgfx