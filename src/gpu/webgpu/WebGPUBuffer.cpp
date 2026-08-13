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
#include "WebGPUCommandQueue.h"
#include "WebGPUGPU.h"
#include "WebGPUUtil.h"
#ifdef TGFX_USE_ASYNCIFY
#include <emscripten.h>
#endif

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
    return mapState == MapState::Mapped;
  }
  return true;
}

#ifdef TGFX_USE_ASYNCIFY
// Asyncify suspends the WASM stack when hitting 'await', yields to the JS event loop,
// and resumes after the Promise resolves. This makes buffer.mapAsync() appear synchronous
// to C++ code without blocking the JS event loop. Asyncify automatically instruments all
// indirect calls (including through virtual function dispatch), unlike JSPI which requires
// explicit export declarations for all entry points.
EM_ASYNC_JS(int, webgpu_buffer_map_sync, (WGPUBuffer bufHandle, size_t size), {
  var bufferWrapper = WebGPU.mgrBuffer.objects[bufHandle];
  if (!bufferWrapper) {
    return 1;
  }
  try {
    await bufferWrapper.object.mapAsync(1 /* GPUMapMode.READ */, 0, size);
    // Initialize onUnmap so that wgpuBufferGetConstMappedRange can register cleanup callbacks.
    // The standard wgpuBufferMapAsync path does this, but we bypass it via EM_ASYNC_JS.
    bufferWrapper.onUnmap = bufferWrapper.onUnmap || [];
    return 0;
  } catch (e) {
    return 1;
  }
});
#endif

void WebGPUBuffer::OnBufferMapped(WGPUBufferMapAsyncStatus status, void* userdata) {
  auto reference = static_cast<std::shared_ptr<MapRequest>*>(userdata);
  if (reference == nullptr) {
    return;
  }
  auto request = *reference;
  delete reference;
  if (request == nullptr || request->owner == nullptr) {
    return;
  }
  auto owner = request->owner;
  if (status == WGPUBufferMapAsyncStatus_Success) {
    owner->mapState = MapState::Mapped;
    return;
  }
  // Falling back to Unmapped is required, otherwise the guard in requestMapAsync() rejects retries.
  owner->mapState = MapState::Unmapped;
  owner->detachMapRequest();
}

void* WebGPUBuffer::map(size_t offset, size_t mapSize) {
  if (buffer == nullptr) {
    return nullptr;
  }
  if (mapSize == GPU_BUFFER_WHOLE_SIZE) {
    mapSize = _size - offset;
  }
  if (_usage & GPUBufferUsage::READBACK) {
    if (mapState != MapState::Mapped) {
      // Expected while the asynchronous mapping is still in flight; callers poll isReady().
      return nullptr;
    }
    return const_cast<void*>(wgpuBufferGetConstMappedRange(buffer, offset, mapSize));
  }
  // WebGPU does not support synchronous buffer mapping. Allocate a CPU-side staging buffer
  // and flush data to the GPU buffer on unmap() via wgpuQueueWriteBuffer.
  if (stagingData != nullptr) {
    free(stagingData);
    stagingData = nullptr;
  }
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
  // Re-issuing a map on a pending buffer is a validation error, and clearing the state of a mapped
  // buffer would drop the obligation to unmap it.
  if (mapState != MapState::Unmapped) {
    return;
  }
  detachMapRequest();
  mapState = MapState::Pending;
#ifdef TGFX_USE_ASYNCIFY
  auto generation = mapGeneration;
  auto result = webgpu_buffer_map_sync(buffer, _size);
  // The WASM stack was suspended during the await, so this request may have been superseded since.
  if (generation != mapGeneration) {
    return;
  }
  mapState = result == 0 ? MapState::Mapped : MapState::Unmapped;
#else
  mapRequest = std::make_shared<MapRequest>();
  mapRequest->owner = this;
  auto reference = new std::shared_ptr<MapRequest>(mapRequest);
  wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, _size, OnBufferMapped, reference);
#endif
}

void WebGPUBuffer::setMapReady(bool ready) {
  if ((_usage & GPUBufferUsage::READBACK) == 0) {
    return;
  }
  // The external path maps the buffer itself, so drop any request issued from C++.
  detachMapRequest();
  mapState = ready ? MapState::Mapped : MapState::Unmapped;
}

void WebGPUBuffer::unmap() {
  if (buffer == nullptr) {
    return;
  }
  if (_usage & GPUBufferUsage::READBACK) {
    if (mapState != MapState::Unmapped) {
      // Unmapping rejects a pending map request, so this doubles as the cancellation path.
      wgpuBufferUnmap(buffer);
      mapState = MapState::Unmapped;
    }
    detachMapRequest();
    return;
  }
  if (stagingData != nullptr) {
    auto commandQueue = static_cast<WebGPUCommandQueue*>(_gpu->queue());
    wgpuQueueWriteBuffer(commandQueue->webgpuQueue(), buffer, stagingOffset, stagingData,
                         stagingSize);
    free(stagingData);
    stagingData = nullptr;
    stagingOffset = 0;
    stagingSize = 0;
  }
}

void WebGPUBuffer::detachMapRequest() {
  // The generation supersedes an in-flight Asyncify request; the owner pointer supersedes a
  // callback-based one.
  ++mapGeneration;
  if (mapRequest != nullptr) {
    mapRequest->owner = nullptr;
    mapRequest = nullptr;
  }
}

void WebGPUBuffer::onRelease(WebGPUGPU*) {
  if (stagingData != nullptr) {
    free(stagingData);
    stagingData = nullptr;
  }
  if (buffer != nullptr) {
    if (mapState != MapState::Unmapped) {
      wgpuBufferUnmap(buffer);
    }
    wgpuBufferDestroy(buffer);
    wgpuBufferRelease(buffer);
    buffer = nullptr;
  }
  mapState = MapState::Unmapped;
  // Required even without a buffer: this object is deleted right after onRelease().
  detachMapRequest();
}

}  // namespace tgfx
