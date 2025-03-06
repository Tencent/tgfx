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

#include "WGLPbufferDevice.h"
#include <mutex>
#include "WGLInterface.h"
#include "WGLUtil.h"
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

  HWND window = CreateWindow(TEXT("WC_TGFX"), TEXT("INVISIBLE_WINDOW"), WS_OVERLAPPEDWINDOW, 0, 0,
                             1, 1, nullptr, nullptr, hInstance, nullptr);
  if (window == nullptr) {
    LOGE("CreateParentWindow() create window failed.");
    return nullptr;
  }
  return window;
}

bool CreatePbufferContext(HDC parentDeviceContext, HGLRC sharedContext, HPBUFFER& pBuffer,
                          HDC& deviceContext, HGLRC& glContext) {
  auto wglInterface = WGLInterface::Get();
  if (!wglInterface->pixelFormatSupport || !wglInterface->pBufferSupport) {
    return false;
  }

  static int pixelFormat = -1;
  static std::once_flag flag;
  std::call_once(flag, [parentDeviceContext] {
    int pixelFormatsToTry[2] = {-1, -1};
    GetPixelFormatsToTry(parentDeviceContext, pixelFormatsToTry);
    pixelFormat = pixelFormatsToTry[0];
  });

  if (pixelFormat == -1) {
    return false;
  }

  pBuffer = wglInterface->wglCreatePbuffer(parentDeviceContext, pixelFormat, 1, 1, nullptr);
  if (pBuffer != nullptr) {
    deviceContext = wglInterface->wglGetPbufferDC(pBuffer);
    if (deviceContext != nullptr) {
      glContext = CreateGLContext(deviceContext, sharedContext);
      if (glContext != nullptr) {
        return true;
      }
      wglInterface->wglReleasePbufferDC(pBuffer, deviceContext);
      deviceContext = nullptr;
    }
    wglInterface->wglDestroyPbuffer(pBuffer);
    pBuffer = nullptr;
  }
  return false;
}
std::shared_ptr<GLDevice> GLDevice::Make(void* sharedContext) {
  HWND window = nullptr;
  HDC parentDeviceContext = nullptr;
  HPBUFFER pBuffer = nullptr;
  HDC deviceContext = nullptr;
  HGLRC glContext = nullptr;
  bool result = false;
  do {
    window = CreateParentWindow();
    if (window == nullptr) {
      LOGE("GLDevice::Make() create window failed!");
      break;
    }
    parentDeviceContext = GetDC(window);
    if (parentDeviceContext == nullptr) {
      LOGE("GLDevice::Make() get device context failed!");
      break;
    }
    if (!CreatePbufferContext(parentDeviceContext, static_cast<HGLRC>(sharedContext), pBuffer,
                              deviceContext, glContext)) {
      LOGE("GLDevice::Make() create pbuffer context failed!");
      break;
    }
    result = true;
  } while (false);

  ReleaseDC(window, parentDeviceContext);
  DestroyWindow(window);

  if (!result) {
    return nullptr;
  }

  auto device = std::shared_ptr<WGLPbufferDevice>(new WGLPbufferDevice(glContext));
  device->externallyOwned = false;
  device->deviceContext = deviceContext;
  device->glContext = glContext;
  device->sharedContext = static_cast<HGLRC>(sharedContext);
  device->pBuffer = pBuffer;
  device->weakThis = device;

  return device;
}

WGLPbufferDevice::WGLPbufferDevice(HGLRC nativeHandle) : WGLDevice(nativeHandle) {
}

WGLPbufferDevice::~WGLPbufferDevice() {
  releaseAll();
  if (externallyOwned || pBuffer == nullptr) {
    return;
  }
  if (glContext != nullptr) {
    wglDeleteContext(glContext);
    glContext = nullptr;
  }

  if (auto wglInterface = WGLInterface::Get();
      wglInterface->pBufferSupport && deviceContext != nullptr) {
    wglInterface->wglReleasePbufferDC(pBuffer, deviceContext);
    wglInterface->wglDestroyPbuffer(pBuffer);
    deviceContext = nullptr;
  }
  pBuffer = nullptr;
}
}  // namespace tgfx