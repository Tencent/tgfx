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
#include <cstdlib>
#include <cstring>
#include "WebGPUGPU.h"
#include "WebGPUUtil.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<WebGPUBuffer> WebGPUBuffer::Make(WebGPUGPU* gpu, size_t size, uint32_t usage) {
  if (gpu == nullptr || size == 0) {
    return nullptr;
  }
  WGPUBufferDescriptor descriptor = {};
  descriptor.size = size;
  descriptor.usage = ToWGPUBufferUsage(usage);
  // WebGPU requires COPY_DST for buffers updated via wgpuQueueWriteBuffer.
  descriptor.usage |= WGPUBufferUsage_CopyDst;
  descriptor.mappedAtCreation = false;
  auto buffer = wgpuDeviceCreateBuffer(gpu->device(), &descriptor);
  if (buffer == nullptr) {
    return nullptr;
  }
  return gpu->makeResource<WebGPUBuffer>(gpu, buffer, size, usage);
}

WebGPUBuffer::WebGPUBuffer(WebGPUGPU* gpu, WGPUBuffer buffer, size_t size, uint32_t usage)
    : GPUBuffer(size, usage), _gpu(gpu), buffer(buffer) {
}

bool WebGPUBuffer::isReady() const {
  if (_usage & GPUBufferUsage::READBACK) {
    return mapReady;
  }
  return true;
}

static void OnBufferMapped(WGPUBufferMapAsyncStatus status, void* userdata) {
  auto ready = static_cast<bool*>(userdata);
  *ready = (status == WGPUBufferMapAsyncStatus_Success);
}

void* WebGPUBuffer::map(size_t offset, size_t mapSize) {
  if (buffer == nullptr) {
    return nullptr;
  }
  if (mapSize == GPU_BUFFER_WHOLE_SIZE) {
    mapSize = _size - offset;
  }
  if (_usage & GPUBufferUsage::READBACK) {
    if (!mapReady) {
      // The buffer has not been asynchronously mapped yet. Synchronous readback is not supported
      // in WebGPU. Call requestMapAsync() first, then poll isReady() before calling map().
      LOGE("WebGPUBuffer::map() readback buffer not mapped. Use requestMapAsync() first.");
      return nullptr;
    }
    return const_cast<void*>(wgpuBufferGetConstMappedRange(buffer, offset, mapSize));
  }
  // WebGPU does not support synchronous buffer mapping. Allocate a CPU-side staging buffer
  // and flush data to the GPU buffer on unmap() via wgpuQueueWriteBuffer.
  stagingData = malloc(mapSize);
  if (stagingData == nullptr) {
    return nullptr;
  }
  memset(stagingData, 0, mapSize);
  stagingOffset = offset;
  stagingSize = mapSize;
  return stagingData;
}

void WebGPUBuffer::requestMapAsync() {
  if (buffer == nullptr || (_usage & GPUBufferUsage::READBACK) == 0) {
    return;
  }
  mapReady = false;
  wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, _size, OnBufferMapped, &mapReady);
}

void WebGPUBuffer::unmap() {
  if (buffer == nullptr) {
    return;
  }
  if (_usage & GPUBufferUsage::READBACK) {
    wgpuBufferUnmap(buffer);
    mapReady = false;
    return;
  }
  if (stagingData != nullptr) {
    auto queue = wgpuDeviceGetQueue(_gpu->device());
    wgpuQueueWriteBuffer(queue, buffer, stagingOffset, stagingData, stagingSize);
    wgpuQueueRelease(queue);
    free(stagingData);
    stagingData = nullptr;
    stagingOffset = 0;
    stagingSize = 0;
  }
}

void WebGPUBuffer::onRelease(WebGPUGPU*) {
  if (stagingData != nullptr) {
    free(stagingData);
    stagingData = nullptr;
  }
  if (buffer != nullptr) {
    wgpuBufferRelease(buffer);
    buffer = nullptr;
  }
}

}  // namespace tgfx
