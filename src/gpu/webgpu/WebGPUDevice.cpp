/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/gpu/webgpu/WebGPUDevice.h"
#include <emscripten/emscripten.h>
#include <emscripten/html5_webgpu.h>
#include <webgpu/webgpu.h>
#include "WebGPUGPU.h"
#include "core/utils/Log.h"

namespace tgfx {

static void OnUncapturedError(WGPUErrorType type, const char* message, void*) {
  const char* typeStr = "Unknown";
  switch (type) {
    case WGPUErrorType_Validation:
      typeStr = "Validation";
      break;
    case WGPUErrorType_OutOfMemory:
      typeStr = "OutOfMemory";
      break;
    case WGPUErrorType_Internal:
      typeStr = "Internal";
      break;
    case WGPUErrorType_DeviceLost:
      typeStr = "DeviceLost";
      break;
    default:
      break;
  }
  LOGE("[WebGPU] Uncaptured %s error: %s", typeStr, message ? message : "(no message)");
}

std::shared_ptr<WebGPUDevice> WebGPUDevice::Make() {
  auto wgpuDevice = emscripten_webgpu_get_device();
  if (wgpuDevice == nullptr) {
    return nullptr;
  }
  wgpuDeviceSetUncapturedErrorCallback(wgpuDevice, OnUncapturedError, nullptr);
  auto gpu = WebGPUGPU::Make(wgpuDevice, false);
  if (gpu == nullptr) {
    wgpuDeviceRelease(wgpuDevice);
    return nullptr;
  }
  auto device = std::shared_ptr<WebGPUDevice>(new WebGPUDevice(std::move(gpu)));
  device->weakThis = device;
  return device;
}

std::shared_ptr<WebGPUDevice> WebGPUDevice::MakeFrom(void* device) {
  if (device == nullptr) {
    return nullptr;
  }
  auto wgpuDevice = static_cast<WGPUDevice>(device);
  wgpuDeviceSetUncapturedErrorCallback(wgpuDevice, OnUncapturedError, nullptr);
  auto gpu = WebGPUGPU::Make(wgpuDevice, true);
  if (gpu == nullptr) {
    return nullptr;
  }
  auto webgpuDevice = std::shared_ptr<WebGPUDevice>(new WebGPUDevice(std::move(gpu)));
  webgpuDevice->weakThis = webgpuDevice;
  return webgpuDevice;
}

WebGPUDevice::WebGPUDevice(std::unique_ptr<WebGPUGPU> gpu) : Device(std::move(gpu)) {
}

WebGPUDevice::~WebGPUDevice() {
  static_cast<WebGPUGPU*>(_gpu)->releaseAll(true);
}

void* WebGPUDevice::webgpuDevice() const {
  return static_cast<WebGPUGPU*>(_gpu)->device();
}

bool WebGPUDevice::onLockContext() {
  return true;
}

void WebGPUDevice::onUnlockContext() {
}

}  // namespace tgfx
