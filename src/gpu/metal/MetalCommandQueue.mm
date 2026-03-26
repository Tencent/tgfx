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

#include "MetalCommandQueue.h"
#include "MetalCommandBuffer.h"
#include "MetalGPU.h"
#include "MetalSemaphore.h"
#include "MetalTexture.h"
#include "MetalUtil.h"
#include "core/utils/Log.h"

namespace tgfx {

MetalCommandQueue::MetalCommandQueue(MetalGPU* metalGPU) : gpu(metalGPU) {
  commandQueue = [metalGPU->device() newCommandQueue];
}

MetalCommandQueue::~MetalCommandQueue() {
  // Wait for the last submitted command buffer to complete before releasing, ensuring all
  // async completion handlers (which capture `this`) have fired before the object is destroyed.
  [lastSubmittedCommandBuffer waitUntilCompleted];
  [lastSubmittedCommandBuffer release];
  [commandQueue release];
}

void MetalCommandQueue::submit(std::shared_ptr<CommandBuffer> commandBuffer) {
  if (!commandBuffer) {
    return;
  }
  @autoreleasepool {
    auto metalCommandBuffer = std::static_pointer_cast<MetalCommandBuffer>(commandBuffer);
    if (metalCommandBuffer->metalCommandBuffer()) {
      // Signal the pending semaphore after this command buffer completes
      if (pendingSignalSemaphore != nullptr) {
        auto value = pendingSignalSemaphore->nextSignalValue();
        [metalCommandBuffer->metalCommandBuffer()
            encodeSignalEvent:pendingSignalSemaphore->metalEvent()
                        value:value];
        pendingSignalSemaphore = nullptr;
      }

      // Present all pending drawables after the command buffer's GPU work completes.
      for (auto& drawable : pendingDrawables) {
        [metalCommandBuffer->metalCommandBuffer() presentDrawable:drawable];
      }
      pendingDrawables.clear();

      auto frameTicks = _frameTime.time_since_epoch().count();
      [metalCommandBuffer->metalCommandBuffer() addCompletedHandler:^(id<MTLCommandBuffer>) {
        _completedFrameTime.store(frameTicks, std::memory_order_release);
      }];

      [metalCommandBuffer->metalCommandBuffer() commit];

      if (lastSubmittedCommandBuffer != nil) {
        [lastSubmittedCommandBuffer release];
      }
      lastSubmittedCommandBuffer = [metalCommandBuffer->metalCommandBuffer() retain];
    }
  }

  // Process unreferenced resources after submission
  gpu->processUnreferencedResources();
}

void MetalCommandQueue::writeBuffer(std::shared_ptr<GPUBuffer> buffer, size_t bufferOffset,
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

void MetalCommandQueue::writeTexture(std::shared_ptr<Texture> texture, const Rect& rect,
                                     const void* pixels, size_t rowBytes) {
  if (!texture || !pixels) {
    LOGE("MetalCommandQueue::writeTexture() invalid parameters: texture=%p, pixels=%p",
         static_cast<void*>(texture.get()), pixels);
    return;
  }

  auto metalTexturePtr = std::static_pointer_cast<MetalTexture>(texture);

  id<MTLTexture> mtlTexture = metalTexturePtr->metalTexture();
  if (!mtlTexture) {
    LOGE("MetalCommandQueue::writeTexture() metalTexture is nil");
    return;
  }

  if (mtlTexture.storageMode == MTLStorageModePrivate) {
    @autoreleasepool {
      auto bytesPerPixel = MTLPixelFormatBytesPerPixel(mtlTexture.pixelFormat);
      NSUInteger width = static_cast<NSUInteger>(rect.width());
      NSUInteger height = static_cast<NSUInteger>(rect.height());
      NSUInteger bytesPerRow =
          rowBytes > 0 ? static_cast<NSUInteger>(rowBytes) : width * bytesPerPixel;
      NSUInteger dataSize = bytesPerRow * height;

      id<MTLBuffer> stagingBuffer = [gpu->device() newBufferWithBytes:pixels
                                                               length:dataSize
                                                              options:MTLResourceStorageModeShared];
      id<MTLCommandBuffer> cmdBuffer = [commandQueue commandBuffer];
      if (cmdBuffer == nil) {
        LOGE("MetalCommandQueue::writeTexture() failed to create command buffer.");
        [stagingBuffer release];
        return;
      }
      id<MTLBlitCommandEncoder> blitEncoder = [cmdBuffer blitCommandEncoder];
      if (blitEncoder == nil) {
        LOGE("MetalCommandQueue::writeTexture() failed to create blit command encoder.");
        [stagingBuffer release];
        return;
      }

      MTLOrigin origin =
          MTLOriginMake(static_cast<NSUInteger>(rect.x()), static_cast<NSUInteger>(rect.y()), 0);
      MTLSize size = MTLSizeMake(width, height, 1);

      [blitEncoder copyFromBuffer:stagingBuffer
                     sourceOffset:0
                sourceBytesPerRow:bytesPerRow
              sourceBytesPerImage:dataSize
                       sourceSize:size
                        toTexture:mtlTexture
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:origin];
      [blitEncoder endEncoding];
      [cmdBuffer commit];
      [cmdBuffer waitUntilCompleted];
      [stagingBuffer release];
    }
  } else {
    MTLRegion region = MTLRegionMake2D(
        static_cast<NSUInteger>(rect.x()), static_cast<NSUInteger>(rect.y()),
        static_cast<NSUInteger>(rect.width()), static_cast<NSUInteger>(rect.height()));
    [mtlTexture replaceRegion:region
                  mipmapLevel:0
                    withBytes:pixels
                  bytesPerRow:static_cast<NSUInteger>(rowBytes)];
  }
}

std::shared_ptr<Semaphore> MetalCommandQueue::insertSemaphore() {
  auto semaphore = MetalSemaphore::Make(gpu);
  if (semaphore == nullptr) {
    return nullptr;
  }
  pendingSignalSemaphore = semaphore;
  return semaphore;
}

void MetalCommandQueue::waitSemaphore(std::shared_ptr<Semaphore> semaphore) {
  if (semaphore == nullptr) {
    return;
  }

  auto metalSemaphore = std::static_pointer_cast<MetalSemaphore>(semaphore);
  if (metalSemaphore->metalEvent() == nil) {
    return;
  }

  pendingWaitSemaphore = metalSemaphore;
}

void MetalCommandQueue::encodePendingWait(id<MTLCommandBuffer> commandBuffer) {
  if (pendingWaitSemaphore == nullptr || commandBuffer == nil) {
    return;
  }
  [commandBuffer encodeWaitForEvent:pendingWaitSemaphore->metalEvent()
                              value:pendingWaitSemaphore->signalValue()];
  pendingWaitSemaphore = nullptr;
}

void MetalCommandQueue::schedulePresent(id<CAMetalDrawable> drawable) {
  pendingDrawables.push_back(drawable);
}

void MetalCommandQueue::waitUntilCompleted() {
  if (lastSubmittedCommandBuffer != nil) {
    [lastSubmittedCommandBuffer waitUntilCompleted];
    [lastSubmittedCommandBuffer release];
    lastSubmittedCommandBuffer = nil;
  }
}

}  // namespace tgfx