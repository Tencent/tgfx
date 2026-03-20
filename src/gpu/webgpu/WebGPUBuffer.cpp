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

#include "WebGPUBuffer.h"
#include "WebGPUGPU.h"
#include "WebGPUUtil.h"

namespace tgfx {

std::shared_ptr<WebGPUBuffer> WebGPUBuffer::Make(WebGPUGPU* gpu, size_t size, uint32_t usage) {
  if (gpu == nullptr || size == 0) {
    return nullptr;
  }
  WGPUBufferDescriptor descriptor = {};
  descriptor.size = size;
  descriptor.usage = ToWGPUBufferUsage(usage);
  descriptor.mappedAtCreation = false;
  auto buffer = wgpuDeviceCreateBuffer(gpu->device(), &descriptor);
  if (buffer == nullptr) {
    return nullptr;
  }
  return gpu->makeResource<WebGPUBuffer>(buffer, size, usage);
}

WebGPUBuffer::WebGPUBuffer(WGPUBuffer buffer, size_t size, uint32_t usage)
    : GPUBuffer(size, usage), buffer(buffer) {
}

bool WebGPUBuffer::isReady() const {
  if (_usage & GPUBufferUsage::READBACK) {
    return mapReady;
  }
  return true;
}

void* WebGPUBuffer::map(size_t offset, size_t mapSize) {
  if (buffer == nullptr) {
    return nullptr;
  }
  if (mapSize == GPU_BUFFER_WHOLE_SIZE) {
    mapSize = _size - offset;
  }
  if (_usage & GPUBufferUsage::READBACK) {
    wgpuBufferMapAsync(buffer, WGPUMapMode_Read, offset, mapSize, nullptr, nullptr);
    mappedPointer = const_cast<void*>(wgpuBufferGetConstMappedRange(buffer, offset, mapSize));
  } else {
    mappedPointer = const_cast<void*>(wgpuBufferGetConstMappedRange(buffer, offset, mapSize));
  }
  return mappedPointer;
}

void WebGPUBuffer::unmap() {
  if (buffer != nullptr && mappedPointer != nullptr) {
    wgpuBufferUnmap(buffer);
    mappedPointer = nullptr;
  }
}

void WebGPUBuffer::onRelease(WebGPUGPU*) {
  if (buffer != nullptr) {
    wgpuBufferRelease(buffer);
    buffer = nullptr;
  }
}

}  // namespace tgfx
