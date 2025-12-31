/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////
#include "tgfx/gpu/webgpu/WebGPUDevice.h"
#include <emscripten/val.h>
#include "core/utils/Log.h"
#include "gpu/webgpu/WebGPUCaps.h"
#include "gpu/webgpu/WebGPUGPU.h"

namespace tgfx {

struct DeviceRequestContext {
  WebGPUDevice* webgpuDevice = nullptr;
  wgpu::Adapter adapter = nullptr;
};

std::shared_ptr<WebGPUDevice> WebGPUDevice::MakeFrom(const std::string& /*canvasID*/,
                                                     std::shared_ptr<ColorSpace> /*colorSpace*/) {
  auto device = std::shared_ptr<WebGPUDevice>(new WebGPUDevice());
  device->weakThis = device;
  return device;
}

WebGPUDevice::WebGPUDevice() : Device(nullptr) {
  instance = wgpu::Instance::Acquire(wgpuCreateInstance(nullptr));
  requestAdapter();
}

void WebGPUDevice::requestAdapter() {
  instance.RequestAdapter(
      nullptr,
      [](WGPURequestAdapterStatus status, WGPUAdapter cAdapter, const char* message,
         void* userdata) {
        if (message) {
          LOGE("RequestAdapter: %s", message);
        }
        if (status != WGPURequestAdapterStatus_Success) {
          return;
        }
        auto* self = static_cast<WebGPUDevice*>(userdata);
        auto adapter = wgpu::Adapter::Acquire(cAdapter);

        wgpu::AdapterInfo info = {};
        adapter.GetInfo(&info);
        printf("adapter vendor: %s\n", info.vendor);
        printf("adapter architecture: %s\n", info.architecture);
        printf("adapter device: %s\n", info.device);
        printf("adapter description: %s\n", info.description);

        auto* context = new DeviceRequestContext{self, adapter};
        adapter.RequestDevice(
            nullptr,
            [](WGPURequestDeviceStatus status, WGPUDevice cDevice, const char* message,
               void* userdata) {
              auto* context = static_cast<DeviceRequestContext*>(userdata);
              if (message) {
                LOGE("RequestDevice: %s", message);
              }
              if (status != WGPURequestDeviceStatus_Success) {
                delete context;
                return;
              }
              auto device = wgpu::Device::Acquire(cDevice);
              auto caps = std::make_shared<WebGPUCaps>(context->adapter, device);
              context->webgpuDevice->setGPU(
                  std::make_unique<WebGPUGPU>(context->adapter, device, caps));
              delete context;
            },
            context);
      },
      this);
}

}  // namespace tgfx