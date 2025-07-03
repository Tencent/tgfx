/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/AtlasManager.h"
#include "core/utils/BlockBuffer.h"
#include "core/utils/Log.h"
#include "core/utils/SlidingWindowTracker.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProgramCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ResourceCache.h"
#include "gpu/ResourceProvider.h"
#include "tgfx/core/Clock.h"

namespace tgfx {
Context::Context(Device* device) : _device(device) {
  // We set the maxBlockSize to 2MB because allocating blocks that are too large can cause memory
  // fragmentation and slow down allocation. It may also increase the application's memory usage due
  // to pre-allocation optimizations on some platforms.
  _drawingBuffer = new BlockBuffer(1 << 14, 1 << 21);  // 16kb, 2MB
  _programCache = new ProgramCache(this);
  _resourceCache = new ResourceCache(this);
  _drawingManager = new DrawingManager(this);
  _resourceProvider = new ResourceProvider(this);
  _proxyProvider = new ProxyProvider(this);
  _maxValueTracker = new SlidingWindowTracker(10);
  _atlasManager = new AtlasManager(this);
}

Context::~Context() {
  // The Device owner must call releaseAll() before deleting this Context, otherwise, GPU resources
  // may leak.
  DEBUG_ASSERT(_resourceCache->empty());
  DEBUG_ASSERT(_programCache->empty());
  delete _programCache;
  delete _resourceCache;
  delete _drawingManager;
  delete _gpu;
  delete _resourceProvider;
  delete _proxyProvider;
  delete _drawingBuffer;
  delete _atlasManager;
  delete _maxValueTracker;
}

bool Context::flush(BackendSemaphore* signalSemaphore) {
  // Clean up all unreferenced resources before flushing, allowing them to be reused. This is
  // particularly crucial for texture resources that are bound to render targets. Only after the
  // cleanup can they be unbound and reused.
  _resourceCache->processUnreferencedResources();
  _atlasManager->preFlush();
  auto flushed = _drawingManager->flush();
  _atlasManager->postFlush();
  bool semaphoreInserted = false;
  if (signalSemaphore != nullptr) {
    auto semaphore = Semaphore::Wrap(signalSemaphore);
    semaphoreInserted = caps()->semaphoreSupport && _gpu->insertSemaphore(semaphore.get());
    if (semaphoreInserted) {
      *signalSemaphore = semaphore->getBackendSemaphore();
    }
  }
  if (flushed) {
    _proxyProvider->purgeExpiredProxies();
    _resourceCache->advanceFrameAndPurge();
    _maxValueTracker->addValue(_drawingBuffer->size());
    _drawingBuffer->clear(_maxValueTracker->getMaxValue());
  }
  return semaphoreInserted;
}

bool Context::submit(bool syncCpu) {
  return _gpu->submitToGpu(syncCpu);
}

void Context::flushAndSubmit(bool syncCpu) {
  flush();
  submit(syncCpu);
}

bool Context::wait(const BackendSemaphore& waitSemaphore) {
  auto semaphore = Semaphore::Wrap(&waitSemaphore);
  if (semaphore == nullptr) {
    return false;
  }
  return caps()->semaphoreSupport && _gpu->waitSemaphore(semaphore.get());
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
  _resourceProvider->releaseAll();
  _programCache->releaseAll(releaseGPU);
  _resourceCache->releaseAll(releaseGPU);
  _atlasManager->releaseAll();
}
}  // namespace tgfx
