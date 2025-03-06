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

#include "WGLInterface.h"
#include <GL/GL.h>
#include <set>
#include <sstream>
#include <string>
#include "core/utils/Log.h"

namespace tgfx {
#define TEMP_CLASS TEXT("TempClass")
static HWND CreateTempWindow() {
  HINSTANCE instance = GetModuleHandle(nullptr);

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

  auto nativeWindow =
      CreateWindow(TEMP_CLASS, TEXT("PlaceholderWindow"), WS_POPUP | WS_CLIPCHILDREN, 0, 0, 8, 8,
                   nullptr, nullptr, instance, nullptr);
  if (nativeWindow == nullptr) {
    UnregisterClass(TEMP_CLASS, instance);
    return nullptr;
  }

  return nativeWindow;
}

static void DestroyTempWindow(HWND nativeWindow) {
  DestroyWindow(nativeWindow);
  HINSTANCE instance = GetModuleHandle(nullptr);
  UnregisterClass(TEMP_CLASS, instance);
}

void InitialiseExtensions(HDC deviceContext, WGLInterface& wglInterface) {
  if (deviceContext == nullptr || wglInterface.wglGetExtensionsString == nullptr) {
    LOGE("InitialiseExtensions() context is invalid");
    return;
  }
  const char* extensionString = wglInterface.wglGetExtensionsString(deviceContext);
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
  wglInterface.pixelFormatSupport =
      extensionList.find("WGL_ARB_pixel_format") != extensionList.end();
  wglInterface.pBufferSupport = extensionList.find("WGL_ARB_pbuffer") != extensionList.end();
}

WGLInterface InitialiseWGL() {
#define GET_PROC(NAME, SUFFIX) \
  wglInterface.wgl##NAME = (NAME##Proc)wglGetProcAddress("wgl" #NAME #SUFFIX)
  WGLInterface wglInterface;
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
    auto deviceContext = GetDC(nativeWindow);
    auto format = ChoosePixelFormat(deviceContext, &descriptor);
    SetPixelFormat(deviceContext, format, &descriptor);
    HGLRC glContext = wglCreateContext(deviceContext);
    DEBUG_ASSERT(glContext);
    wglMakeCurrent(deviceContext, glContext);
    GET_PROC(GetExtensionsString, ARB);
    GET_PROC(ChoosePixelFormat, ARB);
    GET_PROC(CreatePbuffer, ARB);
    GET_PROC(GetPbufferDC, ARB);
    GET_PROC(ReleasePbufferDC, ARB);
    GET_PROC(DestroyPbuffer, ARB);
    InitialiseExtensions(deviceContext, wglInterface);
    wglMakeCurrent(deviceContext, nullptr);
    wglDeleteContext(glContext);
    DestroyTempWindow(nativeWindow);
  }
  wglMakeCurrent(oldDeviceContext, oldGLContext);
  return wglInterface;
}

const WGLInterface* WGLInterface::Get() {
  static WGLInterface instance = InitialiseWGL();
  return &instance;
}
}  // namespace tgfx