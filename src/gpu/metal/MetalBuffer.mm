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

#include "MetalBuffer.h"
#include "MetalDefines.h"
#include "MetalGPU.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<MetalBuffer> MetalBuffer::Make(MetalGPU* gpu, size_t size, uint32_t usage) {
  if (!gpu || size == 0) {
    return nullptr;
  }

  // Create Metal buffer
  MTLResourceOptions options = MetalDefines::ToMTLResourceOptions(usage);
  id<MTLBuffer> metalBuffer = [gpu->device() newBufferWithLength:size options:options];

  if (!metalBuffer) {
    return nullptr;
  }

  return gpu->makeResource<MetalBuffer>(size, usage, metalBuffer);
}

MetalBuffer::MetalBuffer(size_t size, uint32_t usage, id<MTLBuffer> metalBuffer)
    : GPUBuffer(size, usage), buffer(metalBuffer) {
}

void MetalBuffer::onRelease(MetalGPU*) {
  if (pendingCommandBuffer != nil) {
    [pendingCommandBuffer release];
    pendingCommandBuffer = nil;
  }
  if (buffer != nil) {
    [buffer release];
    buffer = nil;
  }
}

void* MetalBuffer::map(size_t offset, size_t size) {
  if (!buffer || mappedPointer != nullptr) {
    return nullptr;
  }
  if (size == 0) {
    LOGE("MetalBuffer::map() size cannot be 0!");
    return nullptr;
  }
  if (size == GPU_BUFFER_WHOLE_SIZE) {
    size = _size - offset;
  }
  if (offset + size > _size) {
    LOGE("MetalBuffer::map() range out of bounds!");
    return nullptr;
  }

  // Check if buffer supports mapping (shared storage mode)
  if (buffer.storageMode != MTLStorageModeShared) {
    LOGE("MetalBuffer::map() buffer storage mode is not Shared, mapping is not supported!");
    return nullptr;
  }

  // For readback buffers, wait for the GPU to finish writing before reading.
  if (pendingCommandBuffer != nil) {
    [pendingCommandBuffer waitUntilCompleted];
    [pendingCommandBuffer release];
    pendingCommandBuffer = nil;
  }

  mappedPointer = static_cast<uint8_t*>(buffer.contents) + offset;
  return mappedPointer;
}

void MetalBuffer::unmap() {
  if (!buffer || mappedPointer == nullptr) {
    return;
  }
  mappedPointer = nullptr;
}

bool MetalBuffer::isReady() const {
  if (pendingCommandBuffer != nil) {
    return pendingCommandBuffer.status == MTLCommandBufferStatusCompleted ||
           pendingCommandBuffer.status == MTLCommandBufferStatusError;
  }
  return buffer != nil;
}

void MetalBuffer::insertReadbackFence(id<MTLCommandBuffer> cmdBuffer) {
  if (pendingCommandBuffer != nil) {
    [pendingCommandBuffer release];
  }
  pendingCommandBuffer = [cmdBuffer retain];
}

}  // namespace tgfx