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

#pragma once

#include <chrono>
#include <deque>
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/Caps.h"
#include "tgfx/gpu/Device.h"

namespace tgfx {
class ProgramCache;
class ResourceCache;
class DrawingManager;
class Gpu;
class ResourceProvider;
class ProxyProvider;
class BlockBuffer;
class SlidingWindowTracker;

/**
 * Context is the main interface to the GPU. It is used to create and manage GPU resources, and to
 * issue drawing commands. Contexts are created by Devices.
 */
class Context {
 public:
  virtual ~Context();

  /**
   * Returns the associated device.
   */
  Device* device() const {
    return _device;
  }

  /**
   * Returns the unique ID of the Context.
   */
  uint32_t uniqueID() const {
    return _device->uniqueID();
  }

  /**
   * Returns the associated cache that manages the lifetime of all Program instances.
   */
  ProgramCache* programCache() const {
    return _programCache;
  }

  /**
   * Returns the associated cache that manages the lifetime of all Resource instances.
   */
  ResourceCache* resourceCache() const {
    return _resourceCache;
  }

  DrawingManager* drawingManager() const {
    return _drawingManager;
  }

  BlockBuffer* drawingBuffer() const {
    return _drawingBuffer;
  }

  ResourceProvider* resourceProvider() const {
    return _resourceProvider;
  }

  ProxyProvider* proxyProvider() const {
    return _proxyProvider;
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
   * frames, it will be automatically purged from the cache. The default value is 10 frames.
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
   * Apply all pending changes to the render target immediately. After issuing all commands, the
   * semaphore will be signaled by the GPU. If the signalSemaphore is not null and uninitialized,
   * a new semaphore is created and initializes BackendSemaphore. The caller must delete the
   * semaphore returned in signalSemaphore. BackendSemaphore can be deleted as soon as this function
   * returns. If the back-end API is OpenGL, only uninitialized backend semaphores are supported.
   * If false is returned, the GPU back-end did not create or add a semaphore to signal on the GPU;
   * the caller should not instruct the GPU to wait on the semaphore.
   */
  bool flush(BackendSemaphore* signalSemaphore = nullptr);

  /**
   * Submit outstanding work to the gpu from all previously un-submitted flushes. The return
   * value of the submit method will indicate whether the submission to the GPU was successful.
   *
   * If the call returns true, all previously passed in semaphores in flush calls will have been
   * submitted to the GPU and they can safely be waited on. The caller should wait on those
   * semaphores or perform some other global synchronization before deleting the semaphores.
   *
   * If it returns false, then those same semaphores will not have been submitted, and we will not
   * try to submit them again. The caller is free to delete the semaphores at any time.
   *
   * If the syncCpu flag is true, this function will return once the gpu has finished with all
   * submitted work.
   */
  bool submit(bool syncCpu = false);

  /**
   * Call to ensure all drawing to the context has been flushed and submitted to the underlying 3D
   * API. This is equivalent to calling Context::flush() followed by Context::submit(syncCpu).
   */
  void flushAndSubmit(bool syncCpu = false);

  /**
   * Returns the GPU backend of this context.
   */
  virtual Backend backend() const = 0;

  /**
   * Returns the capability info of this context.
   */
  virtual const Caps* caps() const = 0;

  /**
   * The Context normally assumes that no outsider is setting state within the underlying GPU API's
   * context/device/whatever. This call informs the context that the state was modified and it
   * should resend. Shouldn't be called frequently for good performance.
   */
  virtual void resetState() = 0;

  Gpu* gpu() {
    return _gpu;
  }

 protected:
  explicit Context(Device* device);

  Gpu* _gpu = nullptr;

 private:
  Device* _device = nullptr;
  ProgramCache* _programCache = nullptr;
  ResourceCache* _resourceCache = nullptr;
  DrawingManager* _drawingManager = nullptr;
  ResourceProvider* _resourceProvider = nullptr;
  ProxyProvider* _proxyProvider = nullptr;
  BlockBuffer* _drawingBuffer = nullptr;
  SlidingWindowTracker* _maxValueTracker = nullptr;

  void releaseAll(bool releaseGPU);

  friend class Device;
  friend class Resource;
};

}  // namespace tgfx
