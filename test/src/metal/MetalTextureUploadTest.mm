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

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include "gpu/metal/MetalCommandBuffer.h"
#include "gpu/metal/MetalCommandQueue.h"
#include "gpu/metal/MetalGPU.h"
#include "gpu/metal/MetalSemaphore.h"
#include "gpu/metal/MetalTexture.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "utils/TestUtils.h"

namespace tgfx {

/**
 * Blocks the GPU queue with a CPU-signaled event. A watchdog releases the event if a regression
 * unexpectedly blocks the test thread, and destruction releases it on every assertion path.
 */
class TextureUploadGate {
 public:
  explicit TextureUploadGate(MetalCommandQueue* queue) {
    @autoreleasepool {
      event = [queue->metalCommandQueue().device newSharedEvent];
      if (event == nil) {
        return;
      }
      commandBuffer = [[queue->metalCommandQueue() commandBuffer] retain];
      if (commandBuffer == nil) {
        return;
      }
      [commandBuffer encodeWaitForEvent:event value:1];
      queue->submit(std::make_shared<MetalCommandBuffer>(commandBuffer));
      watchdog = std::thread(&TextureUploadGate::signalOnTimeout, this);
    }
  }

  ~TextureUploadGate() {
    release();
    if (watchdog.joinable()) {
      watchdog.join();
    }
    [commandBuffer release];
    [event release];
  }

  /**
   * Returns whether the event and its submitted command buffer were created successfully.
   */
  bool isValid() const {
    return event != nil && commandBuffer != nil;
  }

  /**
   * Returns whether the GPU is still blocked by this event.
   */
  bool isClosed() const {
    return event != nil && event.signaledValue == 0;
  }

  /**
   * Returns the event for testing explicit semaphore waits.
   */
  id<MTLSharedEvent> metalEvent() const {
    return event;
  }

  /**
   * Unblocks the GPU and stops the watchdog without accessing the TGFX Context.
   */
  void release() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      released = true;
      event.signaledValue = 1;
    }
    condition.notify_one();
  }

  /**
   * Schedules an independent CPU signal so queue waits can be exercised without deadlocking.
   */
  void releaseAfter(std::chrono::milliseconds delay) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      deadline = std::chrono::steady_clock::now() + delay;
    }
    condition.notify_one();
  }

 private:
  void signalOnTimeout() {
    @autoreleasepool {
      std::unique_lock<std::mutex> lock(mutex);
      while (!released) {
        if (condition.wait_until(lock, deadline) == std::cv_status::timeout &&
            std::chrono::steady_clock::now() >= deadline) {
          event.signaledValue = 1;
          released = true;
          break;
        }
      }
    }
  }

  id<MTLSharedEvent> event = nil;
  id<MTLCommandBuffer> commandBuffer = nil;
  std::mutex mutex = {};
  std::condition_variable condition = {};
  bool released = false;
  std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  std::thread watchdog = {};
};

static std::shared_ptr<MetalTexture> AdoptUploadTexture(MetalGPU* gpu, id<MTLTexture> texture) {
  if (texture == nil) {
    return nullptr;
  }
  MetalTextureInfo info = {};
  info.texture = (__bridge const void*)texture;
  info.format = static_cast<unsigned>(texture.pixelFormat);
  BackendTexture backendTexture(info, static_cast<int>(texture.width),
                                static_cast<int>(texture.height));
  auto imported = gpu->importBackendTexture(
      backendTexture, TextureUsage::TEXTURE_BINDING | TextureUsage::RENDER_ATTACHMENT, true);
  if (imported == nullptr) {
    [texture release];
  }
  return std::static_pointer_cast<MetalTexture>(imported);
}

static std::shared_ptr<MetalTexture> MakeUploadTexture(MetalGPU* gpu, NSUInteger width,
                                                       NSUInteger height,
                                                       MTLStorageMode storageMode,
                                                       MTLHazardTrackingMode hazardTrackingMode,
                                                       bool mipmapped) {
  @autoreleasepool {
    auto descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:mipmapped];
    descriptor.storageMode = storageMode;
    descriptor.hazardTrackingMode = hazardTrackingMode;
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    return AdoptUploadTexture(gpu, [gpu->device() newTextureWithDescriptor:descriptor]);
  }
}

static bool BufferPixelMatches(const std::shared_ptr<GPUBuffer>& buffer,
                               const std::array<uint8_t, 4>& expected) {
  auto pixels = buffer->map(0, 4);
  if (pixels == nullptr) {
    return false;
  }
  std::array<uint8_t, 4> actual = {};
  memcpy(actual.data(), pixels, actual.size());
  buffer->unmap();
  return actual == expected;
}

static bool TexturePixelMatches(MetalGPU* gpu, std::shared_ptr<Texture> texture, int x, int y,
                                const std::array<uint8_t, 4>& expected) {
  auto buffer = gpu->createBuffer(256, GPUBufferUsage::READBACK);
  auto encoder = gpu->createCommandEncoder();
  if (buffer == nullptr || encoder == nullptr) {
    return false;
  }
  encoder->copyTextureToBuffer(std::move(texture), Rect::MakeXYWH(x, y, 1, 1), buffer, 0, 256);
  gpu->queue()->submit(encoder->finish());
  gpu->queue()->waitUntilCompleted();
  return BufferPixelMatches(buffer, expected);
}

TGFX_TEST_PRIVATE(MetalTextureUploadTest, UploadsFlushAtEncoderBoundary) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(gpu->queue());
  queue->waitUntilCompleted();
  auto texture =
      MakeUploadTexture(gpu, 8, 8, MTLStorageModePrivate, MTLHazardTrackingModeTracked, false);
  ASSERT_TRUE(texture != nullptr);
  std::vector<uint8_t> clearPixels(8 * 8 * 4, 0);
  queue->writeTexture(texture, Rect::MakeWH(8, 8), clearPixels.data(), 32);
  queue->waitUntilCompleted();

  TextureUploadGate gate(queue);
  ASSERT_TRUE(gate.isValid());
  std::array<uint8_t, 4> red = {255, 0, 0, 255};
  std::array<uint8_t, 4> green = {0, 255, 0, 255};
  std::vector<uint8_t> pixels(28, 0);
  for (size_t index = 0; index < 3; ++index) {
    memcpy(pixels.data() + index * 4, red.data(), 4);
    memcpy(pixels.data() + 16 + index * 4, green.data(), 4);
  }
  queue->writeTexture(texture, Rect::MakeXYWH(2, 3, 3, 2), pixels.data(), 16);
  EXPECT_TRUE(gate.isClosed());
  EXPECT_TRUE(queue->pendingUploadCommandBuffer != nil);
  EXPECT_EQ(queue->pendingUploadBytes, 24u);
  EXPECT_EQ(queue->pendingUploadCopies, 1u);
  std::fill(pixels.begin(), pixels.end(), 0);
  auto encoder = gpu->createCommandEncoder();
  ASSERT_TRUE(encoder != nullptr);
  EXPECT_TRUE(queue->pendingUploadCommandBuffer == nil);
  EXPECT_EQ(queue->inFlightUploadBytes, 24u);
  gate.release();
  queue->waitUntilCompleted();
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 2, 3, red));
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 4, 4, green));
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 1, 3, {0, 0, 0, 0}));
}

TGFX_TEST_PRIVATE(MetalTextureUploadTest, CopyLimitSplitsBatch) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(gpu->queue());
  queue->waitUntilCompleted();
  auto texture =
      MakeUploadTexture(gpu, 129, 1, MTLStorageModePrivate, MTLHazardTrackingModeTracked, false);
  ASSERT_TRUE(texture != nullptr);
  for (int index = 0; index < 128; ++index) {
    std::array<uint8_t, 4> pixel = {static_cast<uint8_t>(index), 32, 64, 255};
    queue->writeTexture(texture, Rect::MakeXYWH(index, 0, 1, 1), pixel.data(), 4);
  }
  EXPECT_TRUE(queue->pendingUploadCommandBuffer == nil);
  EXPECT_EQ(queue->pendingUploadCopies, 0u);
  ASSERT_EQ(queue->uploadSubmissions.size(), 1u);
  EXPECT_EQ(queue->uploadSubmissions.front().bytes, 128u * 4);
  std::array<uint8_t, 4> lastPixel = {255, 128, 64, 255};
  queue->writeTexture(texture, Rect::MakeXYWH(128, 0, 1, 1), lastPixel.data(), 4);
  EXPECT_EQ(queue->pendingUploadCopies, 1u);
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 0, 0, {0, 32, 64, 255}));
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 127, 0, {127, 32, 64, 255}));
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 128, 0, lastPixel));
}

TGFX_TEST_PRIVATE(MetalTextureUploadTest, LargePaddedUploadSplitsRows) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(gpu->queue());
  queue->waitUntilCompleted();
  auto texture = MakeUploadTexture(gpu, 2050, 2051, MTLStorageModePrivate,
                                   MTLHazardTrackingModeTracked, false);
  ASSERT_TRUE(texture != nullptr);
  std::vector<uint8_t> clearPixels(2050 * 2051 * 4, 0);
  queue->writeTexture(texture, Rect::MakeWH(2050, 2051), clearPixels.data(), 2050 * 4);
  queue->waitUntilCompleted();

  TextureUploadGate gate(queue);
  ASSERT_TRUE(gate.isValid());
  const size_t rowBytes = 2048 * 4 + 16;
  std::vector<uint8_t> pixels(rowBytes * 2048 + 2048 * 4, 0);
  for (size_t row = 0; row < 2049; ++row) {
    for (size_t x = 0; x < 2048; ++x) {
      auto pixel = pixels.data() + row * rowBytes + x * 4;
      pixel[0] = static_cast<uint8_t>(row % 251);
      pixel[1] = 32;
      pixel[2] = 64;
      pixel[3] = 255;
    }
  }
  queue->writeTexture(texture, Rect::MakeXYWH(1, 1, 2048, 2049), pixels.data(), rowBytes);
  EXPECT_TRUE(gate.isClosed());
  ASSERT_EQ(queue->uploadSubmissions.size(), 1u);
  EXPECT_EQ(queue->uploadSubmissions.front().bytes, queue->MAX_UPLOAD_BATCH_BYTES);
  EXPECT_EQ(queue->pendingUploadBytes, 2048u * 4);
  EXPECT_EQ(queue->pendingUploadCopies, 1u);
  std::fill(pixels.begin(), pixels.end(), 0);
  queue->flushTextureUploads();
  gate.release();
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 1, 1, {0, 32, 64, 255}));
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 2048, 2048, {2047 % 251, 32, 64, 255}));
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 2048, 2049, {2048 % 251, 32, 64, 255}));
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 0, 1, {0, 0, 0, 0}));
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 2049, 2050, {0, 0, 0, 0}));
}

TGFX_TEST_PRIVATE(MetalTextureUploadTest, UploadBudgetWaitsForOldestBatch) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(gpu->queue());
  queue->waitUntilCompleted();
  auto texture = MakeUploadTexture(gpu, 4096, 1024, MTLStorageModePrivate,
                                   MTLHazardTrackingModeTracked, false);
  ASSERT_TRUE(texture != nullptr);
  std::vector<uint8_t> pixels(queue->MAX_UPLOAD_BATCH_BYTES, 64);
  TextureUploadGate firstGate(queue);
  ASSERT_TRUE(firstGate.isValid());
  queue->writeTexture(texture, Rect::MakeWH(4096, 1024), pixels.data(), 4096 * 4);
  TextureUploadGate secondGate(queue);
  ASSERT_TRUE(secondGate.isValid());
  for (int index = 0; index < 3; ++index) {
    queue->writeTexture(texture, Rect::MakeWH(4096, 1024), pixels.data(), 4096 * 4);
  }
  EXPECT_EQ(queue->inFlightUploadBytes, queue->MAX_IN_FLIGHT_UPLOAD_BYTES);
  ASSERT_EQ(queue->uploadSubmissions.size(), 4u);
  auto oldest = [queue->uploadSubmissions.front().commandBuffer retain];
  std::fill(pixels.begin(), pixels.end(), 128);
  EXPECT_TRUE(firstGate.isClosed());
  firstGate.releaseAfter(std::chrono::milliseconds(100));
  queue->writeTexture(texture, Rect::MakeWH(4096, 1024), pixels.data(), 4096 * 4);
  EXPECT_FALSE(firstGate.isClosed());
  EXPECT_TRUE(secondGate.isClosed());
  EXPECT_EQ(oldest.status, MTLCommandBufferStatusCompleted);
  EXPECT_LE(queue->inFlightUploadBytes + queue->pendingUploadBytes,
            queue->MAX_IN_FLIGHT_UPLOAD_BYTES);
  [oldest release];
  secondGate.release();
  queue->waitUntilCompleted();
  EXPECT_TRUE(queue->uploadSubmissions.empty());
  EXPECT_EQ(queue->inFlightUploadBytes, 0u);
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 0, 0, {128, 128, 128, 128}));
}

TGFX_TEST_PRIVATE(MetalTextureUploadTest, UntrackedUploadDrainsPriorAccesses) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(gpu->queue());
  queue->waitUntilCompleted();
  auto untracked =
      MakeUploadTexture(gpu, 1, 1, MTLStorageModePrivate, MTLHazardTrackingModeUntracked, false);
  auto tracked =
      MakeUploadTexture(gpu, 1, 1, MTLStorageModePrivate, MTLHazardTrackingModeTracked, false);
  ASSERT_TRUE(untracked != nullptr);
  ASSERT_TRUE(tracked != nullptr);
  std::array<uint8_t, 4> red = {255, 0, 0, 255};
  std::array<uint8_t, 4> green = {0, 255, 0, 255};
  queue->writeTexture(untracked, Rect::MakeWH(1, 1), red.data(), 4);
  EXPECT_TRUE(queue->uploadSubmissions.empty());
  TextureUploadGate gate(queue);
  ASSERT_TRUE(gate.isValid());
  auto buffer = gpu->createBuffer(256, GPUBufferUsage::READBACK);
  auto encoder = gpu->createCommandEncoder();
  ASSERT_TRUE(buffer != nullptr);
  ASSERT_TRUE(encoder != nullptr);
  encoder->copyTextureToBuffer(untracked, Rect::MakeWH(1, 1), buffer, 0, 256);
  queue->submit(encoder->finish());
  auto previous = [queue->lastSubmittedCommandBuffer retain];
  queue->writeTexture(tracked, Rect::MakeWH(1, 1), red.data(), 4);
  EXPECT_TRUE(gate.isClosed());
  gate.releaseAfter(std::chrono::milliseconds(100));
  queue->writeTexture(untracked, Rect::MakeWH(1, 1), green.data(), 4);
  EXPECT_FALSE(gate.isClosed());
  EXPECT_EQ(previous.status, MTLCommandBufferStatusCompleted);
  EXPECT_TRUE(queue->lastSubmittedCommandBuffer == nil);
  EXPECT_TRUE(queue->pendingUploadCommandBuffer == nil);
  EXPECT_TRUE(queue->uploadSubmissions.empty());
  EXPECT_EQ(queue->inFlightUploadBytes, 0u);
  [previous release];
  EXPECT_TRUE(BufferPixelMatches(buffer, red));
  EXPECT_TRUE(TexturePixelMatches(gpu, untracked, 0, 0, green));

  auto nextBuffer = gpu->createBuffer(256, GPUBufferUsage::READBACK);
  auto nextEncoder = gpu->createCommandEncoder();
  ASSERT_TRUE(nextBuffer != nullptr);
  ASSERT_TRUE(nextEncoder != nullptr);
  queue->writeTexture(untracked, Rect::MakeWH(1, 1), red.data(), 4);
  nextEncoder->copyTextureToBuffer(untracked, Rect::MakeWH(1, 1), nextBuffer, 0, 256);
  queue->submit(nextEncoder->finish());
  queue->waitUntilCompleted();
  EXPECT_TRUE(BufferPixelMatches(nextBuffer, red));
}

TGFX_TEST_PRIVATE(MetalTextureUploadTest, EncoderAndSemaphoreFlushUploads) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(gpu->queue());
  queue->waitUntilCompleted();
  auto texture =
      MakeUploadTexture(gpu, 1, 1, MTLStorageModePrivate, MTLHazardTrackingModeTracked, false);
  ASSERT_TRUE(texture != nullptr);
  MetalCommandQueue dependencyQueue(gpu);
  TextureUploadGate gate(&dependencyQueue);
  ASSERT_TRUE(gate.isValid());
  auto waitSemaphore = MetalSemaphore::MakeFrom(gpu, gate.metalEvent(), 1);
  ASSERT_TRUE(waitSemaphore != nullptr);
  queue->waitSemaphore(waitSemaphore);
  std::array<uint8_t, 4> pixel = {32, 64, 128, 255};
  queue->writeTexture(texture, Rect::MakeWH(1, 1), pixel.data(), 4);
  EXPECT_TRUE(queue->pendingWaitSemaphore == nullptr);
  auto signalSemaphore = queue->insertSemaphore();
  ASSERT_TRUE(signalSemaphore != nullptr);
  EXPECT_TRUE(queue->pendingUploadCommandBuffer == nil);

  auto signalEvent = [gpu->device() newSharedEvent];
  ASSERT_TRUE(signalEvent != nil);
  auto observedSignal = MetalSemaphore::MakeFrom(gpu, signalEvent, 0);
  [signalEvent release];
  ASSERT_TRUE(observedSignal != nullptr);
  queue->pendingSignalSemaphore = observedSignal;

  auto buffer = gpu->createBuffer(256, GPUBufferUsage::READBACK);
  auto encoder = gpu->createCommandEncoder();
  ASSERT_TRUE(buffer != nullptr);
  ASSERT_TRUE(encoder != nullptr);
  queue->writeTexture(texture, Rect::MakeWH(1, 1), pixel.data(), 4);
  encoder->copyTextureToBuffer(texture, Rect::MakeWH(1, 1), buffer, 0, 256);
  queue->submit(encoder->finish());
  EXPECT_TRUE(queue->pendingUploadCommandBuffer == nil);
  EXPECT_TRUE(queue->pendingSignalSemaphore == nullptr);

  auto submitted = queue->lastSubmittedCommandBuffer;
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_TRUE(gate.isClosed());
  EXPECT_NE(submitted.status, MTLCommandBufferStatusCompleted);
  EXPECT_NE(submitted.status, MTLCommandBufferStatusError);
  EXPECT_EQ(signalEvent.signaledValue, 0u);

  gate.release();
  queue->waitUntilCompleted();
  EXPECT_GT(signalEvent.signaledValue, 0u);
  EXPECT_EQ(signalEvent.signaledValue, observedSignal->signalValue());
  EXPECT_TRUE(BufferPixelMatches(buffer, pixel));
}

TGFX_TEST_PRIVATE(MetalTextureUploadTest, UploadRetainsNativeResources) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(gpu->queue());
  queue->waitUntilCompleted();
  auto buffer = gpu->createBuffer(256, GPUBufferUsage::READBACK);
  ASSERT_TRUE(buffer != nullptr);
  TextureUploadGate gate(queue);
  ASSERT_TRUE(gate.isValid());
  std::array<uint8_t, 4> expected = {32, 64, 128, 255};
  @autoreleasepool {
    auto texture =
        MakeUploadTexture(gpu, 1, 1, MTLStorageModePrivate, MTLHazardTrackingModeTracked, false);
    ASSERT_TRUE(texture != nullptr);
    std::vector<uint8_t> pixels(expected.begin(), expected.end());
    queue->writeTexture(texture, Rect::MakeWH(1, 1), pixels.data(), 4);
    std::fill(pixels.begin(), pixels.end(), 0);
    std::vector<uint8_t>().swap(pixels);
    auto encoder = gpu->createCommandEncoder();
    ASSERT_TRUE(encoder != nullptr);
    encoder->copyTextureToBuffer(texture, Rect::MakeWH(1, 1), buffer, 0, 256);
    texture = nullptr;
    gpu->processUnreferencedResources();
    queue->submit(encoder->finish());
  }
  EXPECT_TRUE(gate.isClosed());
  gate.release();
  queue->waitUntilCompleted();
  EXPECT_TRUE(BufferPixelMatches(buffer, expected));
}

TGFX_TEST_PRIVATE(MetalTextureUploadTest, SharedUploadsRemainImmediate) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(gpu->queue());
  queue->waitUntilCompleted();
  auto texture =
      MakeUploadTexture(gpu, 1, 1, MTLStorageModeShared, MTLHazardTrackingModeTracked, false);
  ASSERT_TRUE(texture != nullptr);
  std::array<uint8_t, 4> expected = {32, 64, 128, 255};
  auto pixels = expected;
  queue->writeTexture(texture, Rect::MakeWH(1, 1), pixels.data(), 4);
  pixels.fill(0);
  EXPECT_TRUE(queue->pendingUploadCommandBuffer == nil);
  EXPECT_TRUE(queue->uploadSubmissions.empty());
  std::array<uint8_t, 4> actual = {};
  [texture->metalTexture() getBytes:actual.data()
                        bytesPerRow:4
                         fromRegion:MTLRegionMake2D(0, 0, 1, 1)
                        mipmapLevel:0];
  EXPECT_EQ(actual, expected);
}

TGFX_TEST_PRIVATE(MetalTextureUploadTest, MipmapsConsumeAsyncUploads) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(gpu->queue());
  queue->waitUntilCompleted();
  auto texture =
      MakeUploadTexture(gpu, 8, 8, MTLStorageModePrivate, MTLHazardTrackingModeTracked, true);
  ASSERT_TRUE(texture != nullptr);
  std::array<uint8_t, 4> expected = {32, 64, 128, 255};
  std::vector<uint8_t> pixels(8 * 8 * 4);
  for (size_t offset = 0; offset < pixels.size(); offset += 4) {
    memcpy(pixels.data() + offset, expected.data(), 4);
  }
  queue->writeTexture(texture, Rect::MakeWH(8, 8), pixels.data(), 32);
  auto encoder = gpu->createCommandEncoder();
  ASSERT_TRUE(encoder != nullptr);
  EXPECT_TRUE(queue->pendingUploadCommandBuffer == nil);
  encoder->generateMipmapsForTexture(texture);
  queue->submit(encoder->finish());
  queue->waitUntilCompleted();
  auto nativeMip = [texture->metalTexture() newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                              textureType:MTLTextureType2D
                                                                   levels:NSMakeRange(3, 1)
                                                                   slices:NSMakeRange(0, 1)];
  auto mip = AdoptUploadTexture(gpu, nativeMip);
  ASSERT_TRUE(mip != nullptr);
  EXPECT_TRUE(TexturePixelMatches(gpu, mip, 0, 0, expected));
}

TGFX_TEST_PRIVATE(MetalTextureUploadTest, UploadOnlyWaitAndDestruction) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  gpu->queue()->waitUntilCompleted();
  auto texture =
      MakeUploadTexture(gpu, 1, 1, MTLStorageModePrivate, MTLHazardTrackingModeTracked, false);
  ASSERT_TRUE(texture != nullptr);
  std::array<uint8_t, 4> expected = {32, 64, 128, 255};
  {
    MetalCommandQueue queue(gpu);
    queue.writeTexture(texture, Rect::MakeWH(1, 1), expected.data(), 4);
    queue.waitUntilCompleted();
    EXPECT_TRUE(queue.uploadSubmissions.empty());
    EXPECT_EQ(queue.pendingUploadBytes, 0u);
    EXPECT_EQ(queue.inFlightUploadBytes, 0u);
    queue.writeTexture(texture, Rect::MakeWH(1, 1), expected.data(), 4);
    EXPECT_TRUE(queue.pendingUploadCommandBuffer != nil);
    EXPECT_EQ(queue.pendingUploadBytes, 4u);
  }
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 0, 0, expected));
}

TGFX_TEST_PRIVATE(MetalTextureUploadTest, OversizedUploadKeepsBudgetBounded) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(gpu->queue());
  queue->waitUntilCompleted();
  auto texture = MakeUploadTexture(gpu, 4096, 4097, MTLStorageModePrivate,
                                   MTLHazardTrackingModeTracked, false);
  ASSERT_TRUE(texture != nullptr);
  std::vector<uint8_t> pixels(4096u * 4097 * 4, 64);
  TextureUploadGate gate(queue);
  ASSERT_TRUE(gate.isValid());
  gate.releaseAfter(std::chrono::milliseconds(300));
  queue->writeTexture(texture, Rect::MakeWH(4096, 4097), pixels.data(), 4096 * 4);
  EXPECT_LE(queue->pendingUploadBytes + queue->inFlightUploadBytes,
            queue->MAX_IN_FLIGHT_UPLOAD_BYTES);
  EXPECT_EQ(queue->pendingUploadBytes, 4096u * 4);
  EXPECT_EQ(queue->pendingUploadCopies, 1u);
  std::fill(pixels.begin(), pixels.end(), 0);
  queue->flushTextureUploads();
  gate.release();
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 0, 0, {64, 64, 64, 64}));
  EXPECT_TRUE(TexturePixelMatches(gpu, texture, 4095, 4096, {64, 64, 64, 64}));
}

TGFX_TEST_PRIVATE(MetalTextureUploadTest, AlphaUploadPacksPartialRows) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(gpu->queue());
  queue->waitUntilCompleted();
  auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                                       width:3
                                                                      height:2
                                                                   mipmapped:NO];
  descriptor.storageMode = MTLStorageModePrivate;
  descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
  auto texture = AdoptUploadTexture(gpu, [gpu->device() newTextureWithDescriptor:descriptor]);
  ASSERT_TRUE(texture != nullptr);
  std::array<uint8_t, 7> pixels = {16, 32, 64, 0, 128, 192, 255};
  queue->writeTexture(texture, Rect::MakeWH(3, 2), pixels.data(), 4);
  EXPECT_EQ(queue->pendingUploadBytes, 6u);
  queue->flushTextureUploads();
  auto buffer = gpu->createBuffer(512, GPUBufferUsage::READBACK);
  auto encoder = gpu->createCommandEncoder();
  ASSERT_TRUE(buffer != nullptr);
  ASSERT_TRUE(encoder != nullptr);
  encoder->copyTextureToBuffer(texture, Rect::MakeWH(3, 2), buffer, 0, 256);
  queue->submit(encoder->finish());
  queue->waitUntilCompleted();
  auto result = static_cast<const uint8_t*>(buffer->map());
  ASSERT_TRUE(result != nullptr);
  EXPECT_EQ(result[0], 16);
  EXPECT_EQ(result[1], 32);
  EXPECT_EQ(result[2], 64);
  EXPECT_EQ(result[256], 128);
  EXPECT_EQ(result[257], 192);
  EXPECT_EQ(result[258], 255);
  buffer->unmap();
}

static std::shared_ptr<Image> MakeRasterUploadImage(const std::array<uint8_t, 4>& color) {
  Bitmap bitmap(4, 4, false, false, ColorSpace::SRGB());
  auto info =
      ImageInfo::Make(4, 4, ColorType::RGBA_8888, AlphaType::Premultiplied, 0, ColorSpace::SRGB());
  std::array<uint8_t, 4 * 4 * 4> pixels = {};
  for (size_t offset = 0; offset < pixels.size(); offset += 4) {
    memcpy(pixels.data() + offset, color.data(), 4);
  }
  if (!bitmap.writePixels(info, pixels.data())) {
    return nullptr;
  }
  return Image::MakeFrom(bitmap);
}

static bool SurfacePixelMatches(Surface* surface, int x, int y,
                                const std::array<uint8_t, 4>& expected) {
  auto info =
      ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied, 0, ColorSpace::SRGB());
  std::array<uint8_t, 4> pixels = {};
  return surface->readPixels(info, pixels.data(), x, y) && pixels == expected;
}

TGFX_TEST_PRIVATE(MetalTextureUploadTest, DrawingBufferBatchesImageUploads) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(gpu->queue());
  queue->waitUntilCompleted();
  auto surface = Surface::Make(context, 8, 4);
  auto red = MakeRasterUploadImage({255, 0, 0, 255});
  auto green = MakeRasterUploadImage({0, 255, 0, 255});
  ASSERT_TRUE(surface != nullptr);
  ASSERT_TRUE(red != nullptr);
  ASSERT_TRUE(green != nullptr);
  surface->getCanvas()->drawImage(red);
  surface->getCanvas()->drawImage(green, 4, 0);
  auto recording = context->flush();
  ASSERT_TRUE(recording != nullptr);
  TextureUploadGate gate(queue);
  ASSERT_TRUE(gate.isValid());
  context->submit(std::move(recording));
  EXPECT_TRUE(gate.isClosed());
  EXPECT_TRUE(queue->pendingUploadCommandBuffer == nil);
  ASSERT_EQ(queue->uploadSubmissions.size(), 1u);
  EXPECT_EQ(queue->inFlightUploadBytes, 2u * 4 * 4 * 4);
  gate.release();
  EXPECT_TRUE(SurfacePixelMatches(surface.get(), 1, 1, {255, 0, 0, 255}));
  EXPECT_TRUE(SurfacePixelMatches(surface.get(), 5, 1, {0, 255, 0, 255}));
}

TGFX_TEST_PRIVATE(MetalTextureUploadTest, RecordingsPreserveUploadOrder) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto queue = static_cast<MetalCommandQueue*>(gpu->queue());
  queue->waitUntilCompleted();
  auto surface = Surface::Make(context, 8, 4);
  auto red = MakeRasterUploadImage({255, 0, 0, 255});
  auto green = MakeRasterUploadImage({0, 255, 0, 255});
  ASSERT_TRUE(surface != nullptr);
  ASSERT_TRUE(red != nullptr);
  ASSERT_TRUE(green != nullptr);
  surface->getCanvas()->drawImage(red);
  auto firstRecording = context->flush();
  ASSERT_TRUE(firstRecording != nullptr);
  surface->getCanvas()->drawImage(green, 4, 0);
  auto secondRecording = context->flush();
  ASSERT_TRUE(secondRecording != nullptr);
  TextureUploadGate gate(queue);
  ASSERT_TRUE(gate.isValid());
  context->submit(std::move(secondRecording));
  EXPECT_TRUE(gate.isClosed());
  EXPECT_TRUE(queue->pendingWaitSemaphore == nullptr);
  EXPECT_TRUE(queue->pendingSignalSemaphore == nullptr);
  ASSERT_EQ(queue->uploadSubmissions.size(), 2u);
  gate.release();
  context->submit(std::move(firstRecording), true);
  EXPECT_TRUE(SurfacePixelMatches(surface.get(), 1, 1, {255, 0, 0, 255}));
  EXPECT_TRUE(SurfacePixelMatches(surface.get(), 5, 1, {0, 255, 0, 255}));
}

}  // namespace tgfx
