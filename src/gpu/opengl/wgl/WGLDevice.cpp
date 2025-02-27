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
#include <GL/GL.h>
#include "WGLExtensions.h"
#include "WGLPbufferContext.h"
#include "WGLWindowContext.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<GLDevice> GLDevice::Current() {
  auto context = wglGetCurrentContext();
  auto glDevice = GLDevice::Get(context);
  if (glDevice != nullptr) {
    return std::static_pointer_cast<WGLDevice>(glDevice);
  }

  std::unique_ptr<WGLContext> wglContext = std::make_unique<WGLWindowContext>(nullptr, nullptr);
  return WGLDevice::Wrap(std::move(wglContext), true);
}

std::shared_ptr<GLDevice> GLDevice::Make(void* sharedContext) {
  std::unique_ptr<WGLContext> wglContext =
      std::make_unique<WGLPbufferContext>(static_cast<HGLRC>(sharedContext));
  return WGLDevice::Wrap(std::move(wglContext), false);
}

std::shared_ptr<WGLDevice> WGLDevice::MakeFrom(HWND hWnd, HGLRC sharedContext) {
  if (hWnd == nullptr) {
    return nullptr;
  }

  std::unique_ptr<WGLContext> wglContext = std::make_unique<WGLWindowContext>(hWnd, sharedContext);

  if (wglContext == nullptr) {
    LOGE("WGLDevice::MakeFrom() CreateWGLContext failed!");
    return nullptr;
  }
  return WGLDevice::Wrap(std::move(wglContext), false);
}

std::shared_ptr<WGLDevice> WGLDevice::Wrap(std::unique_ptr<WGLContext> wglContext,
                                           bool externallyOwned) {
  if (wglContext == nullptr) {
    return nullptr;
  }
  HGLRC glContext = wglContext->getGLContext();
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
    auto result = wglContext->makeCurrent();
    if (!result) {
      return nullptr;
    }
  }
  auto device = std::shared_ptr<WGLDevice>(new WGLDevice(glContext));
  device->externallyOwned = externallyOwned;
  device->wglContext = std::move(wglContext);
  device->weakThis = device;
  if (oldGLContext != glContext) {
    wglMakeCurrent(oldDeviceContext, oldGLContext);
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
  //wglContext->destroyContext() will be called when destructed;
}

bool WGLDevice::sharableWith(void* nativeConext) const {
  if (wglContext == nullptr) {
    return false;
  }
  return nativeHandle == nativeConext || wglContext->getSharedContext() == nativeConext;
}

bool WGLDevice::onMakeCurrent() {
  DEBUG_ASSERT(wglContext != nullptr);
  return wglContext->makeCurrent();
}

void WGLDevice::onClearCurrent() {
  DEBUG_ASSERT(wglContext != nullptr);
  wglContext->clearCurrent();
}
}  // namespace tgfx