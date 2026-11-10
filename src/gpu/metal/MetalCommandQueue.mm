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
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
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

bool MetalCommandQueue::ensureTextureUploadEncoder() {
  if (pendingUploadEncoder != nil) {
    return true;
  }
  id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
  if (commandBuffer == nil) {
    LOGE("MetalCommandQueue::writeTexture() failed to create command buffer.");
    return false;
  }
  auto waitSemaphore = pendingWaitSemaphore;
  encodePendingWait(commandBuffer);
  id<MTLBlitCommandEncoder> encoder = [commandBuffer blitCommandEncoder];
  if (encoder == nil) {
    pendingWaitSemaphore = std::move(waitSemaphore);
    LOGE("MetalCommandQueue::writeTexture() failed to create blit command encoder.");
    return false;
  }
  pendingUploadCommandBuffer = [commandBuffer retain];
  pendingUploadEncoder = [encoder retain];
  return true;
}

void MetalCommandQueue::flushTextureUploads() {
  if (pendingUploadCommandBuffer == nil) {
    return;
  }
  @autoreleasepool {
    [pendingUploadEncoder endEncoding];
    [pendingUploadEncoder release];
    pendingUploadEncoder = nil;
    [pendingUploadCommandBuffer commit];
    uploadSubmissions.push_back({pendingUploadCommandBuffer, pendingUploadBytes});
    inFlightUploadBytes += pendingUploadBytes;
    pendingUploadCommandBuffer = nil;
    pendingUploadBytes = 0;
    pendingUploadCopies = 0;
  }
}

void MetalCommandQueue::reserveTextureUploadBytes(size_t bytes) {
  DEBUG_ASSERT(bytes <= MAX_UPLOAD_BATCH_BYTES);
  collectCompletedTextureUploads();
  if (bytes > MAX_UPLOAD_BATCH_BYTES - pendingUploadBytes) {
    flushTextureUploads();
  }
  if (bytes > MAX_IN_FLIGHT_UPLOAD_BYTES - inFlightUploadBytes - pendingUploadBytes) {
    flushTextureUploads();
    while (bytes > MAX_IN_FLIGHT_UPLOAD_BYTES - inFlightUploadBytes) {
      [uploadSubmissions.front().commandBuffer waitUntilCompleted];
      collectCompletedTextureUploads();
    }
  }
}

void MetalCommandQueue::collectCompletedTextureUploads() {
  while (!uploadSubmissions.empty()) {
    const auto& submission = uploadSubmissions.front();
    auto status = submission.commandBuffer.status;
    if (status != MTLCommandBufferStatusCompleted && status != MTLCommandBufferStatusError) {
      break;
    }
    inFlightUploadBytes -= submission.bytes;
    [submission.commandBuffer release];
    uploadSubmissions.pop_front();
  }
}

void MetalCommandQueue::waitForTextureUploads() {
  while (!uploadSubmissions.empty()) {
    [uploadSubmissions.front().commandBuffer waitUntilCompleted];
    collectCompletedTextureUploads();
  }
}

MetalCommandQueue::~MetalCommandQueue() {
  // Wait for the last submitted command buffer to complete before releasing, ensuring all
  // async completion handlers (which capture `this`) have fired before the object is destroyed.
  waitUntilCompleted();
  // Release drawables that were never submitted.
  for (auto& drawable : pendingDrawables) {
    [drawable release];
  }
  pendingDrawables.clear();
  [commandQueue release];
}

void MetalCommandQueue::submit(std::shared_ptr<CommandBuffer> commandBuffer) {
  if (!commandBuffer) {
    return;
  }
  flushTextureUploads();
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

      // Present pending drawables after the GPU work; the command buffer keeps them alive.
      for (auto& drawable : pendingDrawables) {
        [metalCommandBuffer->metalCommandBuffer() presentDrawable:drawable];
        [drawable release];
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
  collectCompletedTextureUploads();
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

  if (rect.isEmpty() || !Rect::MakeWH(texture->width(), texture->height()).contains(rect)) {
    return;
  }

  if (mtlTexture.storageMode == MTLStorageModePrivate) {
    @autoreleasepool {
      auto bytesPerPixel = MTLPixelFormatBytesPerPixel(mtlTexture.pixelFormat);
      auto width = static_cast<size_t>(rect.width());
      auto height = static_cast<size_t>(rect.height());
      if (width == 0 || height == 0 || bytesPerPixel == 0 ||
          width > MAX_UPLOAD_BATCH_BYTES / bytesPerPixel) {
        return;
      }
      auto bytesPerRow = width * bytesPerPixel;
      auto sourceRowBytes = rowBytes > 0 ? rowBytes : bytesPerRow;
      if (sourceRowBytes < bytesPerRow ||
          height - 1 > (std::numeric_limits<size_t>::max() - bytesPerRow) / sourceRowBytes) {
        LOGE("MetalCommandQueue::writeTexture() invalid pixel row layout.");
        return;
      }

      // Untracked textures need both prior GPU accesses and this upload to finish before reuse.
      bool synchronize = mtlTexture.hazardTrackingMode == MTLHazardTrackingModeUntracked;
      if (synchronize) {
        waitUntilCompleted();
      }
      auto sourcePixels = static_cast<const uint8_t*>(pixels);
      auto maxRows = MAX_UPLOAD_BATCH_BYTES / bytesPerRow;
      size_t row = 0;
      while (row < height) {
        // Drain transient references per chunk so completed staging buffers can be reclaimed.
        @autoreleasepool {
          auto rows = std::min(height - row, maxRows);
          auto dataSize = bytesPerRow * rows;
          reserveTextureUploadBytes(dataSize);
          id<MTLBuffer> stagingBuffer = nil;
          if (sourceRowBytes == bytesPerRow) {
            stagingBuffer = [gpu->device() newBufferWithBytes:sourcePixels + row * sourceRowBytes
                                                       length:dataSize
                                                      options:MTLResourceStorageModeShared];
          } else {
            // Packing rows bounds staging memory and avoids reading past the final row's pixels.
            stagingBuffer = [gpu->device() newBufferWithLength:dataSize
                                                       options:MTLResourceStorageModeShared];
            if (stagingBuffer != nil) {
              auto destination = static_cast<uint8_t*>(stagingBuffer.contents);
              for (size_t index = 0; index < rows; ++index) {
                memcpy(destination + index * bytesPerRow,
                       sourcePixels + (row + index) * sourceRowBytes, bytesPerRow);
              }
            }
          }
          if (stagingBuffer == nil) {
            LOGE("MetalCommandQueue::writeTexture() failed to create staging buffer.");
            break;
          }
          if (!ensureTextureUploadEncoder()) {
            [stagingBuffer release];
            break;
          }
          auto origin = MTLOriginMake(static_cast<NSUInteger>(rect.x()),
                                      static_cast<NSUInteger>(rect.y()) + row, 0);
          auto size = MTLSizeMake(width, rows, 1);
          [pendingUploadEncoder copyFromBuffer:stagingBuffer
                                  sourceOffset:0
                             sourceBytesPerRow:bytesPerRow
                           sourceBytesPerImage:dataSize
                                    sourceSize:size
                                     toTexture:mtlTexture
                              destinationSlice:0
                              destinationLevel:0
                             destinationOrigin:origin];
          pendingUploadBytes += dataSize;
          ++pendingUploadCopies;
          [stagingBuffer release];
          row += rows;
          if (pendingUploadCopies >= MAX_UPLOAD_BATCH_COPIES ||
              pendingUploadBytes >= MAX_UPLOAD_BATCH_BYTES) {
            flushTextureUploads();
          }
        }
      }
      if (synchronize) {
        waitUntilCompleted();
      }
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
  flushTextureUploads();
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

  flushTextureUploads();
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
  if (drawable == nil) {
    return;
  }
  // Retained manually under MRC; released in submit() or the destructor.
  pendingDrawables.push_back([drawable retain]);
}

void MetalCommandQueue::waitUntilCompleted() {
  flushTextureUploads();
  if (lastSubmittedCommandBuffer != nil) {
    [lastSubmittedCommandBuffer waitUntilCompleted];
    [lastSubmittedCommandBuffer release];
    lastSubmittedCommandBuffer = nil;
  }
  waitForTextureUploads();
}

}  // namespace tgfx