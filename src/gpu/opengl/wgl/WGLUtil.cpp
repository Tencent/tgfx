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
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "core/utils/Log.h"

namespace tgfx {
#if defined(UNICODE)
#define STR_LIT(X) L## #X
#else
#define STR_LIT(X) #X
#endif

#define TEMP_CLASS STR_LIT("TempClass")
static HWND CreateTempWindow() {
  HINSTANCE instance = GetModuleHandle(nullptr);
  RECT windowRect{0, 8, 0, 8};

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

  if (!RegisterClass(&wc)) {
    return nullptr;
  }

  DWORD style = WS_SYSMENU;
  DWORD exStyle = WS_EX_CLIENTEDGE;

  AdjustWindowRectEx(&windowRect, style, false, exStyle);
  HWND nativeWindow = CreateWindowEx(
      exStyle, TEMP_CLASS, STR_LIT("PlaceholderWindows"), WS_CLIPCHILDREN | WS_CLIPSIBLINGS | style,
      0, 0, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, nullptr,
      nullptr, instance, nullptr);
  if (nativeWindow == nullptr) {
    UnregisterClass(TEMP_CLASS, instance);
    return nullptr;
  }
  ShowWindow(nativeWindow, SW_HIDE);
  return nativeWindow;
}

static void DestroyTempWindow(HWND nativeWindow) {
  DestroyWindow(nativeWindow);
  HINSTANCE instance = GetModuleHandle(nullptr);
  UnregisterClass(TEMP_CLASS, instance);
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

static std::set<std::string> ExtensionList;

static void initialiseExtensions(HDC deviceContext) {
  if (deviceContext == nullptr || wglGetExtensionsString == nullptr) {
    LOGE("initialiseExtensions() context is invalid");
    return;
  }
  const char* extensionString = wglGetExtensionsString(deviceContext);
  if (extensionString == nullptr) {
    LOGE("initialiseExtensions() extentionString is nullptr");
    return;
  }
  std::stringstream extensionStream;
  std::string element;
  extensionStream << extensionString;
  while (extensionStream >> element) {
    ExtensionList.insert(element);
  }
}

static void InitialiseWGL() {
  HDC oldDeviceContext = wglGetCurrentDC();
  HGLRC oldGLContext = wglGetCurrentContext();

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
    initialiseExtensions(deviceContet);
    wglMakeCurrent(deviceContet, nullptr);
    wglDeleteContext(glContext);
    DestroyTempWindow(nativeWindow);
  }
  wglMakeCurrent(oldDeviceContext, oldGLContext);
}

static std::atomic<const WGLExtensions*> globalExtensions = {nullptr};
const WGLExtensions* WGLExtensions::Get() {
  auto extensions = globalExtensions.load(std::memory_order_relaxed);
  if (!extensions) {
    static WGLExtensions instance;
    extensions = &instance;
  }
  return extensions;
}

WGLExtensions::WGLExtensions() {
  InitialiseWGL();
}

bool WGLExtensions::hasExtension(const char* ext) const {
  return ExtensionList.find(ext) != ExtensionList.end();
}

BOOL WGLExtensions::choosePixelFormat(HDC deviceContext, const int* attribIList,
                                      const FLOAT* attribFList, UINT maxFormats, int* formats,
                                      UINT* numFormats) const {
  return wglChoosePixelFormat(deviceContext, attribIList, attribFList, maxFormats, formats,
                              numFormats);
}

HPBUFFER WGLExtensions::createPbuffer(HDC deviceContext, int pixelFormat, int width, int height,
                                      const int* attribList) const {
  return wglCreatePbuffer(deviceContext, pixelFormat, width, height, attribList);
}

HDC WGLExtensions::getPbufferDC(HPBUFFER hPbuffer) const {
  return wglGetPbufferDC(hPbuffer);
}

int WGLExtensions::releasePbufferDC(HPBUFFER hPbuffer, HDC deviceContext) const {
  return wglReleasePbufferDC(hPbuffer, deviceContext);
}

BOOL WGLExtensions::destroyPbuffer(HPBUFFER hPbuffer) const {
  return wglDestroyPbuffer(hPbuffer);
}

void GetPixelFormatsToTry(HDC deviceContext, int formatsToTry[2]) {
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

  int* format = formatsToTry[0] ? &formatsToTry[0] : &formatsToTry[1];
  unsigned numFormats;
  constexpr float fAttributes[] = {0, 0};
  auto* extensions = WGLExtensions::Get();
  extensions->choosePixelFormat(deviceContext, iAttributes.data(), fAttributes, 1, format,
                                &numFormats);
}

HGLRC CreateGLContext(HDC deviceContext, HGLRC sharedContext) {
  HDC oldDeviceContext = wglGetCurrentDC();
  HGLRC oldGLContext = wglGetCurrentContext();

  HGLRC glContext = wglCreateContext(deviceContext);
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

}  // namespace tgfx