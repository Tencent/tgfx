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
#include <chrono>
#include "core/AtlasManager.h"
#include "core/AtlasStrikeCache.h"
#include "core/utils/BlockAllocator.h"
#include "core/utils/Log.h"
#include "core/utils/SingleOwner.h"
#include "core/utils/SlidingWindowTracker.h"
#include "core/utils/TileProbe.h"
#include "gpu/DrawingManager.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ResourceCache.h"
#include "gpu/ShaderCaps.h"
#include "tgfx/core/Clock.h"
#include "tgfx/gpu/GPU.h"

#define ASSERT_OWNER_THREAD ASSERT_SINGLE_OWNER(*singleOwner)

// Temporary probe paired with the [TileProbe] lines of DisplayList.cpp: prints a [TileGPU] line
// for every submit whose encode, queue submit or wait takes long. Switched by
// TILE_PROBE_ENABLED in core/utils/TileProbe.h.
#define TILE_GPU_PROBE_ENABLED TILE_PROBE_ENABLED
#if TILE_GPU_PROBE_ENABLED
#define GPU_PROBE(...) __VA_ARGS__
#else
#define GPU_PROBE(...)
#endif

namespace tgfx {

#if TILE_GPU_PROBE_ENABLED
using GPUProbeClock = std::chrono::steady_clock;

static constexpr double GPU_PROBE_SLOW_MS = 2.0;

static double GPUProbeMs(GPUProbeClock::time_point from, GPUProbeClock::time_point to) {
  return std::chrono::duration<double, std::milli>(to - from).count();
}
#endif

Context::Context(Device* device, GPU* gpu) : _device(device), _gpu(gpu) {
#if DEBUG
  singleOwner = std::make_unique<SingleOwner>();
#endif
  _shaderCaps = new ShaderCaps(gpu);
  _globalCache = new GlobalCache(this);
  _resourceCache = new ResourceCache(this);
  _drawingManager = new DrawingManager(this);
  _proxyProvider = new ProxyProvider(this);
  _atlasManager = new AtlasManager(this);
  _atlasStrikeCache = new AtlasStrikeCache();
}

Context::~Context() {
  delete _atlasManager;
  delete _drawingManager;
  delete _globalCache;
  delete _proxyProvider;
  delete _resourceCache;
  delete _shaderCaps;
  delete _atlasStrikeCache;
}

Backend Context::backend() const {
  ASSERT_OWNER_THREAD;
  return _gpu->info()->backend;
}

BlockAllocator* Context::drawingAllocator() const {
  ASSERT_OWNER_THREAD;
  return _drawingManager->drawingAllocator();
}

bool Context::wait(const BackendSemaphore& waitSemaphore) {
  ASSERT_OWNER_THREAD;
  auto semaphore = gpu()->importBackendSemaphore(waitSemaphore);
  if (semaphore == nullptr) {
    return false;
  }
  gpu()->queue()->waitSemaphore(std::move(semaphore));
  return true;
}

std::unique_ptr<Recording> Context::flush(BackendSemaphore* signalSemaphore) {
  ASSERT_OWNER_THREAD;
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
  // The pending queue keeps the buffer and its frames alive until submitted; the Recording
  // keeps the windows alive.
  pendingDrawingBuffers.push_back(drawingBuffer);
  return std::unique_ptr<Recording>(new Recording(uniqueID(), drawingBuffer->uniqueID(),
                                                  drawingBuffer->generation(),
                                                  _drawingManager->takePendingWindows()));
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
  ASSERT_OWNER_THREAD;
  GPU_PROBE(auto probeStart = GPUProbeClock::now(); double probeEncodeMs = 0;
            double probeSubmitMs = 0; double probeWaitMs = 0; int probeBuffers = 0;
            EncodeStats() = {};)
  _resourceCache->processUnreferencedResources();
  auto queue = gpu()->queue();
  auto targetBuffer = getDrawingBuffer(recording.get());
  if (targetBuffer != nullptr) {
    while (!pendingDrawingBuffers.empty()) {
      auto drawingBuffer = pendingDrawingBuffers.front();
      queue->advanceFrameTime();
      _resourceCache->syncFrameTime();
      GPU_PROBE(auto probeEncodeStart = GPUProbeClock::now();)
      auto commandBuffer = drawingBuffer->encode();
      GPU_PROBE(probeEncodeMs += GPUProbeMs(probeEncodeStart, GPUProbeClock::now());
                probeBuffers++;)
      _resourceCache->recordFrameAndPurge();
      bool isLast = (drawingBuffer == targetBuffer);
      if (!isLast) {
        // Insert a GPU fence so that the next command buffer waits for this one to complete
        // before reading any shared render target textures. The semaphore must be inserted
        // before submit() so the signal is encoded into the current command buffer.
        auto semaphore = queue->insertSemaphore();
        if (semaphore != nullptr) {
          queue->waitSemaphore(std::move(semaphore));
        }
      }
      drawingBuffer->attachFrameUsages(this);
      drawingBuffer->schedulePresents(this);
      GPU_PROBE(auto probeSubmitStart = GPUProbeClock::now();)
      queue->submit(std::move(commandBuffer));
      GPU_PROBE(probeSubmitMs += GPUProbeMs(probeSubmitStart, GPUProbeClock::now());)
      drawingBuffer->deliverFrames(this);
      pendingDrawingBuffers.pop_front();
      if (isLast) {
        break;
      }
    }
  }
  if (syncCpu) {
    GPU_PROBE(auto probeWaitStart = GPUProbeClock::now();)
    queue->waitUntilCompleted();
    GPU_PROBE(probeWaitMs = GPUProbeMs(probeWaitStart, GPUProbeClock::now());)
  }
  GPU_PROBE(
      auto probeEnd = GPUProbeClock::now();
      if (probeEncodeMs + probeSubmitMs + probeWaitMs >= GPU_PROBE_SLOW_MS) {
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         probeEnd.time_since_epoch())
                         .count() %
                     1000000;
        auto& enc = EncodeStats();
        auto taskMs = enc.taskMs[0] + enc.taskMs[1] + enc.taskMs[2] + enc.taskMs[3];
        LOGI("[TileGPU] t=%lld buffers=%d encode=%.1f submit=%.1f wait=%.1f total=%.1f | enc "
             "capture=%.1f/%u tiles=%.1f/%u screen=%.1f/%u other=%.1f/%u rest=%.1f discard=%d",
             static_cast<long long>(nowMs), probeBuffers, probeEncodeMs, probeSubmitMs,
             probeWaitMs, GPUProbeMs(probeStart, probeEnd), enc.taskMs[1], enc.tasks[1],
             enc.taskMs[2], enc.tasks[2], enc.taskMs[3], enc.tasks[3], enc.taskMs[0], enc.tasks[0],
             probeEncodeMs - taskMs, TILE_PROBE_DISCARD_CAPTURE);
      })
}

bool Context::flushAndSubmit(bool syncCpu) {
  ASSERT_OWNER_THREAD;
  auto recording = flush();
  bool hasRecording = recording != nullptr;
  if (recording || syncCpu) {
    submit(std::move(recording), syncCpu);
  }
  return hasRecording;
}

size_t Context::memoryUsage() const {
  ASSERT_OWNER_THREAD;
  return _resourceCache->getResourceBytes();
}

size_t Context::purgeableBytes() const {
  ASSERT_OWNER_THREAD;
  return _resourceCache->getPurgeableBytes();
}

size_t Context::cacheLimit() const {
  ASSERT_OWNER_THREAD;
  return _resourceCache->cacheLimit();
}

void Context::setCacheLimit(size_t bytesLimit) {
  ASSERT_OWNER_THREAD;
  _resourceCache->setCacheLimit(bytesLimit);
}

size_t Context::resourceExpirationFrames() const {
  ASSERT_OWNER_THREAD;
  return _resourceCache->expirationFrames();
}

void Context::setResourceExpirationFrames(size_t frames) {
  ASSERT_OWNER_THREAD;
  _resourceCache->setExpirationFrames(frames);
}

void Context::purgeResourcesNotUsedSince(std::chrono::steady_clock::time_point purgeTime) {
  ASSERT_OWNER_THREAD;
  _resourceCache->purgeNotUsedSince(purgeTime);
}

bool Context::purgeResourcesUntilMemoryTo(size_t bytesLimit) {
  ASSERT_OWNER_THREAD;
  return _resourceCache->purgeUntilMemoryTo(bytesLimit);
}
}  // namespace tgfx
