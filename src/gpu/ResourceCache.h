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

#include <deque>
#include <functional>
#include <list>
#include <unordered_map>
#include "core/utils/ReturnQueue.h"
#include "gpu/resources/ResourceKey.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class Resource;

/**
 * Manages the lifetime of all Resource instances.
 */
class ResourceCache {
 public:
  explicit ResourceCache(Context* context);

  ~ResourceCache();

  /**
   * Returns true if there is no cache at all.
   */
  bool empty() const;

  /**
   * Returns the number of bytes consumed by resources.
   */
  size_t getResourceBytes() const {
    return totalBytes;
  }

  /**
   * Returns the number of bytes held by purgeable resources.
   */
  size_t getPurgeableBytes() const {
    return purgeableBytes;
  }

  /**
   * Returns the size of the Context's gpu memory cache limit in bytes. The default value is 512MB.
   */
  size_t cacheLimit() const {
    return maxBytes;
  }

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
  size_t expirationFrames() const {
    return _expirationFrames;
  }

  /**
   * Sets the number of frames (flushes) after which unused GPU resources are considered expired.
   */
  void setExpirationFrames(size_t frames);

  /**
   * Purges GPU resources that haven't been used since the passed point in time.
   * @param purgeTime A time point returned by std::chrono::steady_clock::now() or
   * std::chrono::steady_clock::now() - std::chrono::milliseconds(msNotUsed).
   */
  void purgeNotUsedSince(std::chrono::steady_clock::time_point purgeTime);

  /**
   * Purges GPU resources from the cache until the specified bytesLimit is reached, or until all
   * purgeable resources have been removed. Returns true if the total resource usage does not exceed
   * bytesLimit after purging.
   * @param bytesLimit The target maximum number of bytes after purging.
   */
  bool purgeUntilMemoryTo(size_t bytesLimit);

  /**
   * Advances the frame counter and purges resources that have expired or exceed the cache limit.
   */
  void advanceFrameAndPurge();

  /**
   * Returns a scratch resource in the cache by the specified ScratchKey.
   */
  std::shared_ptr<Resource> findScratchResource(const ScratchKey& scratchKey);

  /**
   * Retrieves a unique resource in the cache by the specified UniqueKey.
   */
  std::shared_ptr<Resource> findUniqueResource(const UniqueKey& uniqueKey);

  /**
   * Returns true if there is a corresponding unique resource for the specified UniqueKey.
   */
  bool hasUniqueResource(const UniqueKey& uniqueKey);

  /**
   * Removes all GPU resources from the cache, regardless of whether they are purgeable or not.
   * Resources that are currently in use will be removed from the cache but not deleted until they
   * are no longer referenced.
   */
  void releaseAll();

 private:
  Context* context = nullptr;
  size_t maxBytes = 512 * (1 << 20);  // 512MB
  size_t totalBytes = 0;
  size_t purgeableBytes = 0;
  // 120 is chosen because a 4K screen can be divided into roughly 120 grids of 256x256 pixels.
  // If each grid is rendered per frame, the cache should cover this use case.
  size_t _expirationFrames = 120;
  std::chrono::steady_clock::time_point currentFrameTime = {};
  std::deque<std::chrono::steady_clock::time_point> frameTimes = {};
  std::shared_ptr<ReturnQueue> returnQueue = ReturnQueue::Make();
  std::list<Resource*> nonpurgeableResources = {};
  std::list<Resource*> purgeableResources = {};
  ResourceKeyMap<std::vector<Resource*>> scratchKeyMap = {};
  ResourceKeyMap<Resource*> uniqueKeyMap = {};

  static void AddToList(std::list<Resource*>& list, Resource* resource);
  static void RemoveFromList(std::list<Resource*>& list, Resource* resource);

  void purgeAsNeeded();
  void processUnreferencedResources();
  std::shared_ptr<Resource> addResource(Resource* resource, const ScratchKey& scratchKey);
  std::shared_ptr<Resource> refResource(Resource* resource);
  void removeResource(Resource* resource);
  void purgeResourcesByLRU(bool scratchResourceOnly,
                           const std::function<bool(Resource*)>& satisfied);

  void changeUniqueKey(Resource* resource, const UniqueKey& uniqueKey);
  void removeUniqueKey(Resource* resource);
  Resource* getUniqueResource(const UniqueKey& uniqueKey);

  friend class Resource;
  friend class Context;
};
}  // namespace tgfx
