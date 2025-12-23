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
#include "core/AtlasManager.h"
#include "core/utils/BlockAllocator.h"
#include "core/utils/Log.h"
#include "core/utils/SlidingWindowTracker.h"
#include "gpu/DrawingManager.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ResourceCache.h"
#include "gpu/ShaderCaps.h"
#include "tgfx/core/Clock.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

Context::Context(Device* device, GPU* gpu) : _device(device), _gpu(gpu) {
  _shaderCaps = new ShaderCaps(gpu);
  _globalCache = new GlobalCache(this);
  _resourceCache = new ResourceCache(this);
  _drawingManager = new DrawingManager(this);
  _proxyProvider = new ProxyProvider(this);
  _atlasManager = new AtlasManager(this);
}

Context::~Context() {
  delete _atlasManager;
  delete _drawingManager;
  delete _globalCache;
  delete _proxyProvider;
  delete _resourceCache;
  delete _shaderCaps;
}

Backend Context::backend() const {
  return _gpu->info()->backend;
}

BlockAllocator* Context::drawingAllocator() const {
  return _drawingManager->drawingAllocator();
}

bool Context::wait(const BackendSemaphore& waitSemaphore) {
  auto semaphore = gpu()->importBackendSemaphore(waitSemaphore);
  if (semaphore == nullptr) {
    return false;
  }
  gpu()->queue()->waitSemaphore(std::move(semaphore));
  return true;
}

std::unique_ptr<Recording> Context::flush(BackendSemaphore* signalSemaphore) {
  _atlasManager->preFlush();
  auto drawingBuffer = _drawingManager->flush();
  if (drawingBuffer == nullptr) {
    return nullptr;
  }
  if (signalSemaphore != nullptr) {
    auto semaphore = gpu()->queue()->insertSemaphore();
    if (semaphore != nullptr) {
      *signalSemaphore = gpu()->stealBackendSemaphore(std::move(semaphore));
    }
  }
  _atlasManager->postFlush();
  _proxyProvider->purgeExpiredProxies();
  pendingDrawingBuffers.push_back(drawingBuffer);
  return std::unique_ptr<Recording>(
      new Recording(uniqueID(), drawingBuffer->uniqueID(), drawingBuffer->generation()));
}

std::shared_ptr<DrawingBuffer> Context::getDrawingBuffer(const Recording* recording) const {
  if (recording == nullptr) {
    return nullptr;
  }
  if (recording->contextID != uniqueID()) {
    LOGE(
        "Context::getDrawingBuffer() Recording was created by a different Context and cannot be "
        "submitted.");
    return nullptr;
  }
  for (const auto& drawingBuffer : pendingDrawingBuffers) {
    if (drawingBuffer->uniqueID() == recording->drawingBufferID &&
        drawingBuffer->generation() == recording->generation) {
      return drawingBuffer;
    }
  }
  return nullptr;
}

void Context::submit(std::unique_ptr<Recording> recording, bool syncCpu) {
  _resourceCache->processUnreferencedResources();
  auto queue = gpu()->queue();
  auto targetBuffer = getDrawingBuffer(recording.get());
  if (targetBuffer != nullptr) {
    while (!pendingDrawingBuffers.empty()) {
      auto drawingBuffer = pendingDrawingBuffers.front();
      auto commandBuffer = drawingBuffer->encode();
      _resourceCache->advanceFrameAndPurge();
      queue->submit(std::move(commandBuffer));
      pendingDrawingBuffers.pop_front();
      if (drawingBuffer == targetBuffer) {
        break;
      }
    }
  }
  if (syncCpu) {
    queue->waitUntilCompleted();
  }
}

bool Context::flushAndSubmit(bool syncCpu) {
  auto recording = flush();
  bool hasRecording = recording != nullptr;
  if (recording || syncCpu) {
    submit(std::move(recording), syncCpu);
  }
  return hasRecording;
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
}  // namespace tgfx
