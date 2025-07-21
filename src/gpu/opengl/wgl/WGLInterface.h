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

namespace tgfx {
DECLARE_HANDLE(HPBUFFER);

#define WGL_DRAW_TO_WINDOW 0x2001
#define WGL_ACCELERATION 0x2003
#define WGL_SUPPORT_OPENGL 0x2010
#define WGL_DOUBLE_BUFFER 0x2011
#define WGL_COLOR_BITS 0x2014
#define WGL_RED_BITS 0x2015
#define WGL_GREEN_BITS 0x2017
#define WGL_BLUE_BITS 0x2019
#define WGL_ALPHA_BITS 0x201B
#define WGL_STENCIL_BITS 0x2023
#define WGL_FULL_ACCELERATION 0x2027
#define WGL_CONTEXT_MAJOR_VERSION 0x2091
#define WGL_CONTEXT_MINOR_VERSION 0x2092
#define WGL_CONTEXT_PROFILE_MASK 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT 0x00000001

using GetExtensionsStringProc = const char*(WINAPI*)(HDC);
using ChoosePixelFormatProc = BOOL(WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
using CreatePbufferProc = HPBUFFER(WINAPI*)(HDC, int, int, int, const int*);
using GetPbufferDCProc = HDC(WINAPI*)(HPBUFFER);
using ReleasePbufferDCProc = int(WINAPI*)(HPBUFFER, HDC);
using DestroyPbufferProc = BOOL(WINAPI*)(HPBUFFER);
using SwapIntervalProc = BOOL(WINAPI*)(int);
using CreateContextAttribsProc = HGLRC(WINAPI*)(HDC, HGLRC, const int*);

class WGLInterface {
 public:
  /**
   * Returns the static WGLInterface instance
   */
  static const WGLInterface* Get();

  bool pixelFormatSupport = false;
  bool pBufferSupport = false;
  bool swapIntervalSupport = false;
  bool createContextAttribsSupport = false;

  int glMajorMax = 1;
  int glMinorMax = 0;

  GetExtensionsStringProc wglGetExtensionsString = nullptr;
  ChoosePixelFormatProc wglChoosePixelFormat = nullptr;
  CreatePbufferProc wglCreatePbuffer = nullptr;
  GetPbufferDCProc wglGetPbufferDC = nullptr;
  ReleasePbufferDCProc wglReleasePbufferDC = nullptr;
  DestroyPbufferProc wglDestroyPbuffer = nullptr;
  SwapIntervalProc wglSwapInterval = nullptr;
  CreateContextAttribsProc wglCreateContextAttribs = nullptr;
};
}  // namespace tgfx
