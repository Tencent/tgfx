/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/gpu/opengl/GLDevice.h"
#include <thread>
#include "gpu/opengl/GLGPU.h"

namespace tgfx {
static std::mutex deviceMapLocker = {};
static std::unordered_map<void*, GLDevice*> deviceMap = {};

std::shared_ptr<GLDevice> GLDevice::MakeWithFallback() {
  auto device = GLDevice::Make();
  if (device != nullptr) {
    return device;
  }
#ifndef TGFX_BUILD_FOR_WEB
  for (auto& item : deviceMap) {
    device = std::static_pointer_cast<GLDevice>(item.second->weakThis.lock());
    if (device != nullptr && !device->externallyOwned) {
      LOGE(
          "GLDevice::MakeWithFallback(): Failed to create a new GLDevice! Fall back to the "
          "existing one.");
      return device;
    }
  }
#endif
  return GLDevice::Current();
}

std::shared_ptr<GLDevice> GLDevice::Get(void* nativeHandle) {
  if (nativeHandle == nullptr) {
    return nullptr;
  }
  std::lock_guard<std::mutex> autoLock(deviceMapLocker);
  auto result = deviceMap.find(nativeHandle);
  if (result != deviceMap.end()) {
    auto device = result->second->weakThis.lock();
    if (device) {
      return std::static_pointer_cast<GLDevice>(device);
    }
    deviceMap.erase(result);
  }
  return nullptr;
}

GLDevice::GLDevice(void* nativeHandle) : nativeHandle(nativeHandle) {
  std::lock_guard<std::mutex> autoLock(deviceMapLocker);
  deviceMap[nativeHandle] = this;
}

GLDevice::~GLDevice() {
  std::lock_guard<std::mutex> autoLock(deviceMapLocker);
  deviceMap.erase(nativeHandle);
}

bool GLDevice::onLockContext() {
  if (!onMakeCurrent()) {
    return false;
  }
  if (context == nullptr) {
    auto gpu = GLGPU::MakeNative();
    if (gpu != nullptr) {
      context = new Context(this, std::move(gpu));
    } else {
      LOGE("GLDevice::onLockContext(): Error on creating the GPU! ");
    }
  }
  if (context == nullptr) {
    onClearCurrent();
    return false;
  }
  return true;
}

void GLDevice::onUnlockContext() {
  onClearCurrent();
}
}  // namespace tgfx