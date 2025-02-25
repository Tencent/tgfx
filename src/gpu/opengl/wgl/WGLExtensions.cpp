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

#include "tgfx/gpu/opengl/wgl/WGLExtensions.h"
#include "core/utils/Log.h"
#include <mutex>

namespace tgfx {

#if defined(UNICODE)
#define STR_LIT(X) L## #X
#else
#define STR_LIT(X) #X
#endif

#define TEMP_CLASS STR_LIT("TempClass")
static HWND CreateTempWindow() {
  HINSTANCE instance = GetModuleHandle(nullptr);
  RECT windowRect{0,8,0,8};

  WNDCLASS wc;
  wc.style = CS_HREDRAW|CS_VREDRAW|CS_OWNDC;
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

  AdjustWindowRectEx(&windowRect,style,false, exStyle);
  HWND hWnd = CreateWindowEx(exStyle,
                              TEMP_CLASS,
                              STR_LIT("PlaceholderWindows"),
                              WS_CLIPCHILDREN|WS_CLIPSIBLINGS|style,
                              0,0,
                              windowRect.right - windowRect.left,
                              windowRect.bottom - windowRect.top,
                              nullptr, nullptr,
                              instance,
                              nullptr);
  if (hWnd == nullptr) {
    UnregisterClass(TEMP_CLASS, instance);
    return nullptr;
  }
  ShowWindow(hWnd, SW_HIDE);
  return hWnd;
}

static void DestroyTempWindow(HWND hWnd) {
  DestroyWindow(hWnd);
  HINSTANCE instance = GetModuleHandle(nullptr);
  UnregisterClass(TEMP_CLASS, instance);
}

#define GET_PROC(NAME, SUFFIX) wgl##NAME = \
(NAME##Proc) wglGetProcAddress("wgl" #NAME #SUFFIX)

using GetExtensionsStringProc = const char* (WINAPI *)(HDC);
using ChoosePixelFormatProc = BOOL (WINAPI*)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);
using SwapIntervalProc = BOOL (WINAPI*)(int);
using CreatePbufferProc = HPBUFFER (WINAPI*)(HDC,int,int,int,const int*);
using GetPbufferDCProc = HDC (WINAPI*)(HPBUFFER);
using ReleasePbufferDCProc = int (WINAPI*)(HPBUFFER,HDC);
using DestroyPbufferProc = BOOL (WINAPI*)(HPBUFFER);

static GetExtensionsStringProc wglGetExtensionsString = nullptr;
static ChoosePixelFormatProc wglChoosePixelFormat = nullptr;
static SwapIntervalProc wglSwapInterval = nullptr;
static CreatePbufferProc wglCreatePbuffer = nullptr;
static GetPbufferDCProc wglGetPbufferDC = nullptr;
static ReleasePbufferDCProc wglReleasePbufferDC = nullptr;
static DestroyPbufferProc wglDestroyPbuffer = nullptr;

WGLExtensions::WGLExtensions(){
  static std::once_flag flag;
  std::call_once(flag, [this]{
    HDC oldDC = wglGetCurrentDC();
    HGLRC oldGLRC = wglGetCurrentContext();

    PIXELFORMATDESCRIPTOR pfd;
    ZeroMemory(&pfd,sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 0;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    HWND hWnd = CreateTempWindow();
    if (hWnd) {
      HDC dc = GetDC(hWnd);
      int format = ChoosePixelFormat(dc,&pfd);
      SetPixelFormat(dc, format, &pfd);
      HGLRC glrc = wglCreateContext(dc);
      DEBUG_ASSERT(glrc);
      wglMakeCurrent(dc,glrc);
      GET_PROC(GetExtensionsString, ARB);
      GET_PROC(ChoosePixelFormat, ARB);
      GET_PROC(SwapInterval, EXT);
      GET_PROC(CreatePbuffer, ARB);
      GET_PROC(GetPbufferDC, ARB);
      GET_PROC(ReleasePbufferDC,ARB);
      GET_PROC(DestroyPbuffer, ARB);
      wglMakeCurrent(dc, nullptr);
      wglDeleteContext(glrc);
      DestroyTempWindow(hWnd);
    }
    wglMakeCurrent(oldDC, oldGLRC);
  });
}

bool WGLExtensions::hasExtension(HDC dc, const char* ext) const{
  if (nullptr == wglGetExtensionsString) {
    return false;
  }
  if (0 == strcmp("WGL_ARB_extensions_string", ext)) {
    return true;
  }
  const char* extensionString = wglGetExtensionsString(dc);
  size_t extLength = strlen(ext);

  while (true) {
    size_t n = strcspn(extensionString, " ");
    if (n == extLength && 0 == strncmp(ext, extensionString, n)) {
      return true;
    }
    if (0 == extensionString[n]) {
      return false;
    }
    extensionString += n+1;
  }
}


BOOL WGLExtensions::choosePixelFormat(HDC hdc,
                                      const int* attribIList,
                                      const FLOAT* attribFList,
                                      UINT maxFormats,
                                      int* formats,
                                      UINT* numFormats) const{
  return wglChoosePixelFormat(hdc, attribIList, attribFList,
                              maxFormats, formats, numFormats);
}

BOOL WGLExtensions::swapInterval(int interval) const{
  return wglSwapInterval(interval);
}

HPBUFFER WGLExtensions::createPbuffer(HDC dc,
                                      int pixelFormat,
                                      int width,
                                      int height,
                                      const int* attribList) const{
  return wglCreatePbuffer(dc,pixelFormat,width,height,attribList);
}

HDC WGLExtensions::getPbufferDC(HPBUFFER hPbuffer) const{
  return wglGetPbufferDC(hPbuffer);
}

int WGLExtensions::releasePbufferDC(HPBUFFER hPbuffer, HDC dc) const{
  return wglReleasePbufferDC(hPbuffer,dc);
}

BOOL WGLExtensions::destroyPbuffer(HPBUFFER hPbuffer) const{
  return wglDestroyPbuffer(hPbuffer);
}

}  // namespace tgfx