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
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/val.h>
#include "core/utils/Log.h"
#include "gpu/webgpu/WebGPUCaps.h"
#include "gpu/webgpu/WebGPUGPU.h"

namespace tgfx {

struct DeviceRequestContext {
  WebGPUDevice* webgpuDevice = nullptr;
  wgpu::Instance instance = nullptr;
  wgpu::Adapter adapter = nullptr;
};

std::shared_ptr<WebGPUDevice> WebGPUDevice::MakeFrom(const std::string& canvasID,
                                                     std::shared_ptr<ColorSpace> /*colorSpace*/) {
  if (canvasID.empty()) {
    return nullptr;
  }
  auto device = std::shared_ptr<WebGPUDevice>(new WebGPUDevice(canvasID));
  device->weakThis = device;
  return device;
}

WebGPUDevice::WebGPUDevice(const std::string& canvasID) : Device(nullptr), canvasID(canvasID) {
  instance = wgpu::Instance::Acquire(wgpuCreateInstance(nullptr));

  // Create wgpu::Surface from canvas
  wgpu::SurfaceDescriptorFromCanvasHTMLSelector canvasDesc = {};
  canvasDesc.selector = canvasID.c_str();
  wgpu::SurfaceDescriptor surfaceDesc = {};
  surfaceDesc.nextInChain = &canvasDesc;
  surface = instance.CreateSurface(&surfaceDesc);
  if (surface == nullptr) {
    LOGE("WebGPUDevice::WebGPUDevice() failed to create surface from canvas: %s", canvasID.c_str());
  }

  requestAdapter();
}

void WebGPUDevice::requestAdapter() {
  auto self = this;
  instance.RequestAdapter(
      nullptr,
      [](WGPURequestAdapterStatus status, WGPUAdapter cAdapter, const char* message,
         void* userdata) {
        auto self = static_cast<WebGPUDevice*>(userdata);
        if (message) {
          LOGE("RequestAdapter: %s", message);
        }
        if (status != WGPURequestAdapterStatus_Success) {
          return;
        }
        auto adapter = wgpu::Adapter::Acquire(cAdapter);

        wgpu::AdapterInfo info = {};
        adapter.GetInfo(&info);

        auto context = std::make_unique<DeviceRequestContext>(
            DeviceRequestContext{self, self->instance, adapter});
        adapter.RequestDevice(
            nullptr,
            [](WGPURequestDeviceStatus status, WGPUDevice cDevice, const char* message,
               void* userdata) {
              std::unique_ptr<DeviceRequestContext> context(
                  static_cast<DeviceRequestContext*>(userdata));
              if (message) {
                LOGE("RequestDevice: %s", message);
              }
              if (status != WGPURequestDeviceStatus_Success) {
                return;
              }
              auto device = wgpu::Device::Acquire(cDevice);
              auto caps = std::make_shared<WebGPUCaps>(context->adapter, device);
              context->webgpuDevice->setGPU(
                  std::make_unique<WebGPUGPU>(context->instance, context->adapter, device, caps));
            },
            context.release());
      },
      self);
}

bool WebGPUDevice::configureSurface(int width, int height) {
  if (surface == nullptr) {
    return false;
  }
  auto webgpuGPU = static_cast<WebGPUGPU*>(_gpu);
  if (webgpuGPU == nullptr) {
    return false;
  }
  if (width <= 0 || height <= 0) {
    LOGE("WebGPUDevice::configureSurface() invalid size: %dx%d", width, height);
    return false;
  }
  if (surfaceWidth == width && surfaceHeight == height) {
    return false;
  }

  wgpu::SurfaceConfiguration config = {};
  config.device = webgpuGPU->wgpuDevice();
  config.format = textureFormat;
  config.width = static_cast<uint32_t>(width);
  config.height = static_cast<uint32_t>(height);
  config.usage = wgpu::TextureUsage::RenderAttachment;
  config.presentMode = wgpu::PresentMode::Fifo;
  config.alphaMode = wgpu::CompositeAlphaMode::Premultiplied;
  surface.Configure(&config);

  surfaceWidth = width;
  surfaceHeight = height;
  return true;
}

bool WebGPUDevice::onLockContext() {
  // WebGPU does not have a "current context" concept like OpenGL. Each WebGPU device operates
  // independently without requiring explicit context switching. We only need to check if the
  // GPU has been initialized (since WebGPU device creation is asynchronous on web platforms).
  return _gpu != nullptr;
}

void WebGPUDevice::onUnlockContext() {
  // WebGPU does not require any cleanup when unlocking because there is no global context state
  // to restore. Unlike OpenGL which requires restoring the previous context, WebGPU devices are
  // independent and stateless.
}

}  // namespace tgfx