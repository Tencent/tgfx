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

#include <functional>
#include <list>
#include <unordered_map>
#include "gpu/ResourceKey.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class Resource;

/**
 * Manages the lifetime of all Resource instances.
 */
class ResourceCache {
 public:
  explicit ResourceCache(Context* context);

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
   * Returns current cache limits of max gpu memory byte size.
   */
  size_t cacheLimit() const {
    return maxBytes;
  }

  /**
   * Sets the cache limits of max gpu memory byte size.
   */
  void setCacheLimit(size_t bytesLimit);

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
   * Purges GPU resources that haven't been used since the passed point in time.
   * @param purgeTime A time point previously returned by std::chrono::steady_clock::now().
   * @param scratchResourcesOnly If true, the purgeable resources containing unique keys are spared.
   * If false, then all purgeable resources will be deleted.
   */
  void purgeNotUsedSince(std::chrono::steady_clock::time_point purgeTime,
                         bool scratchResourcesOnly = false);

  /**
   * Purge unreferenced resources from the cache until the provided bytesLimit has been reached,
   * or we have purged all unreferenced resources. Returns true if the total resource bytes is not
   * over the specified bytesLimit after purging.
   * @param bytesLimit The desired number of bytes after puring.
   * @param scratchResourcesOnly If true, the purgeable resources containing unique keys are spared.
   * If false, then all purgeable resources will be deleted.
   */
  bool purgeUntilMemoryTo(size_t bytesLimit, bool scratchResourcesOnly = false);

  /**
   * Purge unreferenced resources not used since the specific time point until the default
   * cacheLimit is reached.
   */
  bool purgeToCacheLimit(std::chrono::steady_clock::time_point notUsedSinceTime);

 private:
  Context* context = nullptr;
  size_t maxBytes = 0;
  size_t totalBytes = 0;
  size_t purgeableBytes = 0;
  std::list<Resource*> nonpurgeableResources = {};
  std::list<Resource*> purgeableResources = {};
  ScratchKeyMap<std::vector<Resource*>> scratchKeyMap = {};
  std::unordered_map<uint32_t, Resource*> uniqueKeyMap = {};

  static void AddToList(std::list<Resource*>& list, Resource* resource);
  static void RemoveFromList(std::list<Resource*>& list, Resource* resource);
  static bool InList(const std::list<Resource*>& list, Resource* resource);

  void releaseAll(bool releaseGPU);
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
