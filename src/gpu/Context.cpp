/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/gpu/Context.h"
#include "GPU.h"
#include "core/AtlasManager.h"
#include "core/utils/BlockBuffer.h"
#include "core/utils/Log.h"
#include "core/utils/SlidingWindowTracker.h"
#include "gpu/DrawingManager.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ResourceCache.h"
#include "tgfx/core/Clock.h"

namespace tgfx {
Context::Context(Device* device, std::unique_ptr<GPU> gpu) : _device(device), _gpu(gpu.release()) {
  // We set the maxBlockSize to 2MB because allocating blocks that are too large can cause memory
  // fragmentation and slow down allocation. It may also increase the application's memory usage due
  // to pre-allocation optimizations on some platforms.
  _drawingBuffer = new BlockBuffer(1 << 14, 1 << 21);  // 16kb, 2MB
  _globalCache = new GlobalCache(this);
  _resourceCache = new ResourceCache(this);
  _drawingManager = new DrawingManager(this);
  _proxyProvider = new ProxyProvider(this);
  _maxValueTracker = new SlidingWindowTracker(10);
  _atlasManager = new AtlasManager(this);
}

Context::~Context() {
  // The Device owner must call releaseAll() before deleting this Context, otherwise, GPU resources
  // may leak.
  DEBUG_ASSERT(_resourceCache->empty());
  delete _globalCache;
  delete _resourceCache;
  delete _drawingManager;
  delete _proxyProvider;
  delete _drawingBuffer;
  delete _atlasManager;
  delete _maxValueTracker;
  delete _gpu;
}

Backend Context::backend() const {
  return _gpu->backend();
}

const Caps* Context::caps() const {
  return _gpu->caps();
}

bool Context::wait(const BackendSemaphore& waitSemaphore) {
  auto semaphore = Semaphore::Wrap(this, waitSemaphore);
  if (semaphore == nullptr) {
    return false;
  }
  _drawingManager->addSemaphoreWaitTask(std::move(semaphore));
  return true;
}

bool Context::flush(BackendSemaphore* signalSemaphore) {
  _resourceCache->processUnreferencedResources();
  _atlasManager->preFlush();
  commandBuffer = _drawingManager->flush(signalSemaphore);
  if (commandBuffer == nullptr) {
    return false;
  }
  _atlasManager->postFlush();
  _proxyProvider->purgeExpiredProxies();
  _resourceCache->advanceFrameAndPurge();
  _maxValueTracker->addValue(_drawingBuffer->size());
  _drawingBuffer->clear(_maxValueTracker->getMaxValue());
  return true;
}

bool Context::submit(bool syncCpu) {
  if (commandBuffer == nullptr) {
    return false;
  }
  auto queue = gpu()->queue();
  queue->submit(std::move(commandBuffer));
  if (syncCpu) {
    queue->waitUntilCompleted();
  }
  return true;
}

void Context::flushAndSubmit(bool syncCpu) {
  flush();
  submit(syncCpu);
}

size_t Context::memoryUsage() const {
  return _resourceCache->getResourceBytes();
}

size_t Context::purgeableBytes() const {
  return _resourceCache->getPurgeableBytes();
}

size_t Context::cacheLimit() const {
  return _resourceCache->cacheLimit();
}

void Context::setCacheLimit(size_t bytesLimit) {
  _resourceCache->setCacheLimit(bytesLimit);
}

size_t Context::resourceExpirationFrames() const {
  return _resourceCache->expirationFrames();
}

void Context::setResourceExpirationFrames(size_t frames) {
  _resourceCache->setExpirationFrames(frames);
}

void Context::purgeResourcesNotUsedSince(std::chrono::steady_clock::time_point purgeTime) {
  _resourceCache->purgeNotUsedSince(purgeTime);
}

bool Context::purgeResourcesUntilMemoryTo(size_t bytesLimit) {
  return _resourceCache->purgeUntilMemoryTo(bytesLimit);
}

void Context::releaseAll(bool releaseGPU) {
  _drawingManager->releaseAll();
  _atlasManager->releaseAll();
  _globalCache->releaseAll();
  _resourceCache->releaseAll(releaseGPU);
}
}  // namespace tgfx
