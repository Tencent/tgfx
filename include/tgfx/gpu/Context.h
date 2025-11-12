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

#pragma once

#include <chrono>
#include <deque>
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/Device.h"

namespace tgfx {
class GlobalCache;
class ResourceCache;
class DrawingManager;
class GPU;
class ResourceProvider;
class ProxyProvider;
class BlockBuffer;
class SlidingWindowTracker;
class AtlasManager;
class CommandBuffer;
class ShaderCaps;

/**
 * Context is responsible for creating and managing GPU resources, as well as issuing drawing
 * commands. It is not thread-safe and should only be used from the single thread where it was
 * locked from the Device. After unlocking the Context from the Device, do not use it further as
 * this may result in undefined behavior.
 */
class Context {
 public:
  /**
   * Creates a new Context with the specified device and GPU backend.
   */
  Context(Device* device, GPU* gpu);

  ~Context();

  /**
   * Returns the associated device.
   */
  Device* device() const {
    return _device;
  }

  /**
   * Returns the GPU backend type of this context.
   */
  Backend backend() const;

  /**
   * Returns the shader capability info of the backend GPU.
   */
  const ShaderCaps* shaderCaps() const {
    return _shaderCaps;
  }

  /**
   * Returns the GPU instance associated with this context.
   */
  GPU* gpu() const {
    return _gpu;
  }

  /**
   * Returns the unique ID of the Context.
   */
  uint32_t uniqueID() const {
    return _device->uniqueID();
  }

  /**
   * Returns the number of bytes consumed by internal gpu caches.
   */
  size_t memoryUsage() const;

  /**
   * Returns the number of bytes held by purgeable resources.
   */
  size_t purgeableBytes() const;

  /**
   * Returns the size of the Context's gpu memory cache limit in bytes. The default value is 512MB.
   */
  size_t cacheLimit() const;

  /**
   * Sets the size of the Context's gpu memory cache limit in bytes. If the new limit is lower than
   * the current limit, the cache will try to free resources to get under the new limit.
   */
  void setCacheLimit(size_t bytesLimit);

  /**
   * Returns the number of frames (valid flushes) after which unused GPU resources are considered
   * expired. A 'frame' is defined as a non-empty flush where actual rendering work is performed and
   * commands are submitted to the GPU. If a GPU resource is not used for more than this number of
   * frames, it will be automatically purged from the cache. The default value is 120 frames.
   */
  size_t resourceExpirationFrames() const;

  /**
   * Sets the number of frames (valid flushes) after which unused GPU resources are considered
   * expired. If the new value is lower than the current value, the cache will try to free resources
   * that haven't been used for more than the new number of frames.
   */
  void setResourceExpirationFrames(size_t frames);

  /**
   * Purges GPU resources that haven't been used since the passed point in time.
   * @param purgeTime A time point returned by std::chrono::steady_clock::now() or
   * std::chrono::steady_clock::now() - std::chrono::milliseconds(msNotUsed).
   */
  void purgeResourcesNotUsedSince(std::chrono::steady_clock::time_point purgeTime);

  /**
   * Purges GPU resources from the cache until the specified bytesLimit is reached, or until all
   * purgeable resources have been removed. Returns true if the total resource usage does not exceed
   * bytesLimit after purging.
   * @param bytesLimit The target maximum number of bytes after purging.
   */
  bool purgeResourcesUntilMemoryTo(size_t bytesLimit);

  /**
   * Inserts a GPU semaphore that the current GPU-backed API must wait on before executing any more
   * commands on the GPU. The context will take ownership of the underlying semaphore and delete it
   * once it has been signaled and waited on. If this call returns false, then the GPU back-end will
   * not wait on the passed semaphore, and the client will still own the semaphore. Returns true if
   * GPU is waiting on the semaphore.
   */
  bool wait(const BackendSemaphore& waitSemaphore);

  /**
   * Ensures that all pending drawing operations for this context are flushed to the underlying GPU
   * API objects. A call to Context::submit is always required to ensure work is actually sent to
   * the GPU. If signalSemaphore is not null, a new semaphore will be created and assigned to
   * signalSemaphore. The caller is responsible for deleting the semaphore returned in
   * signalSemaphore. Returns false if there are no pending drawing operations and nothing was
   * flushed to the GPU. In that case, signalSemaphore will not be initialized, and the caller
   * should not wait on it.
   */
  bool flush(BackendSemaphore* signalSemaphore = nullptr);

  /**
   * Submit outstanding work to the gpu from all previously un-submitted flushes.
   * If the syncCpu flag is true, this function will return once the gpu has finished with all
   * submitted work.
   */
  void submit(bool syncCpu = false);

  /**
   * Call to ensure all drawing to the context has been flushed and submitted to the underlying 3D
   * API. This is equivalent to calling Context::flush() followed by Context::submit(syncCpu).
   *
   * Returns false if there are no pending drawing operations and nothing was flushed to the GPU.
   */
  bool flushAndSubmit(bool syncCpu = false);

  GlobalCache* globalCache() const {
    return _globalCache;
  }

  ResourceCache* resourceCache() const {
    return _resourceCache;
  }

  DrawingManager* drawingManager() const {
    return _drawingManager;
  }

  BlockBuffer* drawingBuffer() const {
    return _drawingBuffer;
  }

  ProxyProvider* proxyProvider() const {
    return _proxyProvider;
  }

  AtlasManager* atlasManager() const {
    return _atlasManager;
  }

 private:
  Device* _device = nullptr;
  GPU* _gpu = nullptr;
  ShaderCaps* _shaderCaps = nullptr;
  GlobalCache* _globalCache = nullptr;
  ResourceCache* _resourceCache = nullptr;
  DrawingManager* _drawingManager = nullptr;
  ProxyProvider* _proxyProvider = nullptr;
  BlockBuffer* _drawingBuffer = nullptr;
  SlidingWindowTracker* _maxValueTracker = nullptr;
  AtlasManager* _atlasManager = nullptr;
  std::shared_ptr<CommandBuffer> commandBuffer = nullptr;
};

}  // namespace tgfx
