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

#include "MtlBuffer.h"
#include "MtlGPU.h"
#include "MtlDefines.h"

namespace tgfx {

std::shared_ptr<MtlBuffer> MtlBuffer::Make(MtlGPU* gpu, size_t size, uint32_t usage) {
  if (!gpu || size == 0) {
    return nullptr;
  }
  
  // Create Metal buffer
  MTLResourceOptions options = MtlDefines::ToMTLResourceOptions(usage);
  id<MTLBuffer> mtlBuffer = [gpu->device() newBufferWithLength:size options:options];
  
  if (!mtlBuffer) {
    return nullptr;
  }
  
  return gpu->makeResource<MtlBuffer>(size, usage, mtlBuffer);
}

MtlBuffer::MtlBuffer(size_t size, uint32_t usage, id<MTLBuffer> mtlBuffer)
    : GPUBuffer(size, usage), buffer(mtlBuffer) {
}

void MtlBuffer::onRelease(MtlGPU*) {
  if (pendingCommandBuffer != nil) {
    [pendingCommandBuffer release];
    pendingCommandBuffer = nil;
  }
  if (buffer != nil) {
    [buffer release];
    buffer = nil;
  }
}

void* MtlBuffer::map(size_t offset, size_t size) {
  if (!buffer || mappedPointer != nullptr) {
    return nullptr;
  }
  
  // Check if buffer supports mapping (shared storage mode)
  if (buffer.storageMode != MTLStorageModeShared) {
    return nullptr;
  }
  
  // For readback buffers, wait for the GPU to finish writing before reading.
  if (pendingCommandBuffer != nil) {
    [pendingCommandBuffer waitUntilCompleted];
    [pendingCommandBuffer release];
    pendingCommandBuffer = nil;
  }

  mappedPointer = static_cast<uint8_t*>(buffer.contents) + offset;
  (void)size;
  
  return mappedPointer;
}

void MtlBuffer::unmap() {
  if (!buffer || mappedPointer == nullptr) {
    return;
  }
  mappedPointer = nullptr;
}

bool MtlBuffer::isReady() const {
  if (pendingCommandBuffer != nil) {
    return pendingCommandBuffer.status == MTLCommandBufferStatusCompleted ||
           pendingCommandBuffer.status == MTLCommandBufferStatusError;
  }
  return buffer != nil;
}

void MtlBuffer::insertReadbackFence(id<MTLCommandBuffer> cmdBuffer) {
  if (pendingCommandBuffer != nil) {
    [pendingCommandBuffer release];
  }
  pendingCommandBuffer = [cmdBuffer retain];
}

}  // namespace tgfx