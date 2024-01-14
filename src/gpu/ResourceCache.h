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
   * Returns a recycled resource in the cache by the specified recycle key.
   */
  std::shared_ptr<Resource> findRecycledResource(const BytesKey& recycleKey);

  /**
   * Retrieves the corresponding resource in the cache by the specified ResourceKey.
   */
  std::shared_ptr<Resource> getResource(const ResourceKey& resourceKey);

  /**
   * Returns true if there is a corresponding resource for the specified ResourceKey.
   */
  bool hasResource(const ResourceKey& resourceKey);

  /**
   * Purges GPU resources that haven't been used the passed in time.
   * @param purgeTime A timestamp previously returned by Clock::Now().
   * @param recycledResourceOnly If true, purgeable resources with external weak references are
   * spared. If false, all purgeable resources will be deleted.
   */
  void purgeNotUsedSince(int64_t purgeTime, bool recycledResourceOnly = false);

  /**
   * Purge unreferenced resources from the cache until the provided bytesLimit has been reached,
   * or we have purged all unreferenced resources. Returns true if the total resource bytes is not
   * over the specified bytesLimit after purging.
   * @param bytesLimit The desired number of bytes after puring.
   * @param recycledResourceOnly If true, purgeable resources with external weak references are
   * spared. If false, all purgeable resources will be deleted.
   */
  bool purgeUntilMemoryTo(size_t bytesLimit, bool recycledResourceOnly = false);

 private:
  Context* context = nullptr;
  size_t maxBytes = 0;
  size_t totalBytes = 0;
  size_t purgeableBytes = 0;
  std::list<Resource*> nonpurgeableResources = {};
  std::list<Resource*> purgeableResources = {};
  BytesKeyMap<std::vector<Resource*>> recycleKeyMap = {};
  std::unordered_map<uint64_t, Resource*> resourceKeyMap = {};

  static void AddToList(std::list<Resource*>& list, Resource* resource);
  static void RemoveFromList(std::list<Resource*>& list, Resource* resource);
  static bool InList(const std::list<Resource*>& list, Resource* resource);

  void releaseAll(bool releaseGPU);
  void processUnreferencedResources();
  std::shared_ptr<Resource> addResource(Resource* resource, const BytesKey& recycleKey);
  std::shared_ptr<Resource> refResource(Resource* resource);
  void removeResource(Resource* resource);
  void purgeResourcesByLRU(bool recycledResourceOnly,
                           const std::function<bool(Resource*)>& satisfied);

  void changeResourceKey(Resource* resource, const ResourceKey& resourceKey);
  void removeResourceKey(Resource* resource);
  Resource* getUniqueResource(const ResourceKey& resourceKey);

  friend class Resource;
  friend class Context;
};
}  // namespace tgfx
