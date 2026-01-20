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
#include "core/utils/Log.h"

namespace tgfx {
WebGPUBuffer::WebGPUBuffer(wgpu::Buffer buffer, wgpu::Queue queue, size_t size, uint32_t usage)
    : GPUBuffer(size, usage), _buffer(buffer), _queue(queue) {
}

WebGPUBuffer::~WebGPUBuffer() {
  if (localBuffer != nullptr) {
    free(localBuffer);
    localBuffer = nullptr;
  }
  if (_buffer != nullptr) {
    _buffer.Destroy();
  }
}

bool WebGPUBuffer::isReady() const {
  return true;
}

void* WebGPUBuffer::map(size_t offset, size_t size) {
  if (localBuffer != nullptr) {
    LOGE("WebGPUBuffer::map() you must call unmap() before mapping again.");
    return nullptr;
  }
  if (size == 0) {
    LOGE("WebGPUBuffer::map() size cannot be 0!");
    return nullptr;
  }
  if (size == GPU_BUFFER_WHOLE_SIZE) {
    size = _size - offset;
  }
  if (offset + size > _size) {
    LOGE("WebGPUBuffer::map() range out of bounds!");
    return nullptr;
  }

  localBuffer = malloc(size);
  if (localBuffer != nullptr) {
    mappedOffset = offset;
    mappedSize = size;
    return localBuffer;
  }
  return nullptr;
}

void WebGPUBuffer::unmap() {
  if (localBuffer == nullptr) {
    return;
  }
  _queue.WriteBuffer(_buffer, mappedOffset, localBuffer, mappedSize);
  free(localBuffer);
  localBuffer = nullptr;
}
}  // namespace tgfx
