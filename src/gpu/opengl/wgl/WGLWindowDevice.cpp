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

#include "WGLWindowDevice.h"
#include "WGLUtil.h"

namespace tgfx {

std::shared_ptr<WGLDevice> WGLDevice::MakeFrom(HWND nativeWindow, HGLRC sharedContext) {
  if (nativeWindow == nullptr) {
    return nullptr;
  }
  HDC deviceContext = nullptr;
  HGLRC glContext = nullptr;
  if (!CreateWGLContext(nativeWindow, sharedContext, deviceContext, glContext)) {
    return nullptr;
  }
  return WGLDevice::Wrap(nativeWindow, deviceContext, glContext, sharedContext, false);
}

std::shared_ptr<WGLDevice> WGLDevice::Wrap(HWND nativeWindow, HDC deviceContext, HGLRC glContext,
                                           HGLRC sharedContext, bool externallyOwned) {
  auto glDevice = GLDevice::Get(glContext);
  if (glDevice != nullptr) {
    return std::static_pointer_cast<WGLDevice>(glDevice);
  }
  if (glContext == nullptr) {
    return nullptr;
  }

  auto oldDeviceContext = wglGetCurrentDC();
  auto oldGLContext = wglGetCurrentContext();
  if (oldGLContext != glContext) {
    auto result = wglMakeCurrent(deviceContext, glContext);
    if (!result) {
      return nullptr;
    }
  }
  auto device = std::shared_ptr<WGLWindowDevice>(new WGLWindowDevice(glContext));
  device->externallyOwned = externallyOwned;
  device->deviceContext = deviceContext;
  device->glContext = glContext;
  device->sharedContext = sharedContext;
  device->nativeWindow = nativeWindow;
  device->weakThis = device;
  if (oldGLContext != glContext) {
    wglMakeCurrent(deviceContext, nullptr);
    if (oldDeviceContext) {
      wglMakeCurrent(oldDeviceContext, oldGLContext);
    }
  }
  return device;
}

WGLWindowDevice::WGLWindowDevice(HGLRC nativeHandle) : WGLDevice(nativeHandle) {
}

WGLWindowDevice::~WGLWindowDevice() {
  releaseAll();
  if (externallyOwned || nativeWindow == nullptr) {
    return;
  }
  if (glContext != nullptr) {
    wglDeleteContext(glContext);
    glContext = nullptr;
  }

  if (deviceContext != nullptr) {
    ReleaseDC(nativeWindow, deviceContext);
    deviceContext = nullptr;
  }
  nativeWindow = nullptr;
}
}  // namespace tgfx