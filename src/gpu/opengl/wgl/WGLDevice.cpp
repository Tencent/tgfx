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

#include "tgfx/gpu/opengl/wgl/WGLDevice.h"

namespace tgfx {
void* GLDevice::CurrentNativeHandle() {
  return wglGetCurrentContext();
}

std::shared_ptr<GLDevice> GLDevice::Current() {
  auto glContext = wglGetCurrentContext();
  auto glDevice = GLDevice::Get(glContext);
  if (glDevice != nullptr) {
    return std::static_pointer_cast<WGLDevice>(glDevice);
  }
  auto deviceContext = wglGetCurrentDC();
  return WGLDevice::Wrap(nullptr, deviceContext, glContext, nullptr, true);
}

WGLDevice::WGLDevice(HGLRC nativeHandle) : GLDevice(nativeHandle) {
}

bool WGLDevice::sharableWith(void* nativeConext) const {
  return nativeHandle == nativeConext || sharedContext == nativeConext;
}

bool WGLDevice::onMakeCurrent() {
  oldGLContext = wglGetCurrentContext();
  oldDeviceContext = wglGetCurrentDC();
  if (oldGLContext == glContext) {
    return true;
  }
  if (!wglMakeCurrent(deviceContext, glContext)) {
    return false;
  }
  return true;
}

void WGLDevice::onClearCurrent() {
  if (oldGLContext == glContext) {
    return;
  }
  wglMakeCurrent(deviceContext, nullptr);
  if (oldDeviceContext != nullptr) {
    wglMakeCurrent(oldDeviceContext, oldGLContext);
  }
}
}  // namespace tgfx