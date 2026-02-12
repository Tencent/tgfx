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

#include "MtlCommandQueue.h"
#include "MtlGPU.h"
#include "MtlCommandBuffer.h"
#include "MtlSemaphore.h"
#include "MtlTexture.h"

namespace tgfx {

MtlCommandQueue::MtlCommandQueue(MtlGPU* mtlGPU) : gpu(mtlGPU) {
  commandQueue = [mtlGPU->device() newCommandQueue];
}

MtlCommandQueue::~MtlCommandQueue() {
  if (commandQueue != nil) {
    [commandQueue release];
    commandQueue = nil;
  }
}

void MtlCommandQueue::submit(std::shared_ptr<CommandBuffer> commandBuffer) {
  if (!commandBuffer) {
    return;
  }
  
  auto mtlCommandBuffer = std::static_pointer_cast<MtlCommandBuffer>(commandBuffer);
  if (mtlCommandBuffer && mtlCommandBuffer->mtlCommandBuffer()) {
    // Signal the pending semaphore if one was created
    if (pendingSemaphore != nullptr) {
      auto value = pendingSemaphore->nextSignalValue();
      [mtlCommandBuffer->mtlCommandBuffer() encodeSignalEvent:pendingSemaphore->mtlEvent()
                                                        value:value];
      pendingSemaphore = nullptr;
    }
    
    [mtlCommandBuffer->mtlCommandBuffer() commit];
  }
  
  // Process unreferenced resources after submission
  gpu->processUnreferencedResources();
}

void MtlCommandQueue::writeBuffer(std::shared_ptr<GPUBuffer> buffer, size_t bufferOffset, 
                                 const void* data, size_t dataSize) {
  if (!buffer || !data || dataSize == 0) {
    return;
  }
  
  // Map buffer and copy data
  void* mappedData = buffer->map(bufferOffset, dataSize);
  if (mappedData) {
    memcpy(mappedData, data, dataSize);
    buffer->unmap();
  }
}

void MtlCommandQueue::writeTexture(std::shared_ptr<Texture> texture, const Rect& rect, 
                                  const void* pixels, size_t rowBytes) {
  if (!texture || !pixels) {
    NSLog(@"MtlCommandQueue::writeTexture() invalid parameters: texture=%p, pixels=%p", 
          static_cast<void*>(texture.get()), pixels);
    return;
  }
  
  auto mtlTexture = std::static_pointer_cast<MtlTexture>(texture);
  if (!mtlTexture) {
    NSLog(@"MtlCommandQueue::writeTexture() failed to cast to MtlTexture");
    return;
  }
  
  id<MTLTexture> metalTexture = mtlTexture->mtlTexture();
  if (!metalTexture) {
    NSLog(@"MtlCommandQueue::writeTexture() mtlTexture is nil");
    return;
  }
  
  // Calculate the region to update
  MTLRegion region = MTLRegionMake2D(static_cast<NSUInteger>(rect.x()),
                                     static_cast<NSUInteger>(rect.y()),
                                     static_cast<NSUInteger>(rect.width()),
                                     static_cast<NSUInteger>(rect.height()));
  
  // Use replaceRegion for shared storage mode textures
  [metalTexture replaceRegion:region
                  mipmapLevel:0
                    withBytes:pixels
                  bytesPerRow:static_cast<NSUInteger>(rowBytes)];
}

std::shared_ptr<Semaphore> MtlCommandQueue::insertSemaphore() {
  auto semaphore = MtlSemaphore::Make(gpu);
  if (semaphore == nullptr) {
    return nullptr;
  }
  pendingSemaphore = semaphore;
  return semaphore;
}

void MtlCommandQueue::waitSemaphore(std::shared_ptr<Semaphore> semaphore) {
  if (semaphore == nullptr) {
    return;
  }
  
  auto mtlSemaphore = std::static_pointer_cast<MtlSemaphore>(semaphore);
  if (mtlSemaphore == nullptr || mtlSemaphore->mtlEvent() == nil) {
    return;
  }
  
  // Create a command buffer to encode the wait
  id<MTLCommandBuffer> cmdBuffer = [commandQueue commandBuffer];
  if (cmdBuffer == nil) {
    return;
  }
  
  [cmdBuffer encodeWaitForEvent:mtlSemaphore->mtlEvent() value:mtlSemaphore->signalValue()];
  [cmdBuffer commit];
}

void MtlCommandQueue::waitUntilCompleted() {
  // Create and commit an empty command buffer to synchronize
  id<MTLCommandBuffer> cmdBuffer = [commandQueue commandBuffer];
  if (cmdBuffer != nil) {
    [cmdBuffer commit];
    [cmdBuffer waitUntilCompleted];
  }
}

}  // namespace tgfx