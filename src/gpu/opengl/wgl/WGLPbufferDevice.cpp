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
#include "WGLUtil.h"

namespace tgfx {

std::shared_ptr<GLDevice> GLDevice::Make(void* sharedContext) {
  HPBUFFER pBuffer = nullptr;
  HDC deviceContext = nullptr;
  HGLRC glContext = nullptr;

  if (!CreatePbufferContext(static_cast<HGLRC>(sharedContext), pBuffer, deviceContext, glContext)) {
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

  if (deviceContext != nullptr) {
    ReleasePbufferDC(pBuffer, deviceContext);
    DestroyPbuffer(pBuffer);
    deviceContext = nullptr;
  }
  pBuffer = nullptr;
}
}  // namespace tgfx