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

#include "gpu/ResourceCache.h"
#include <unordered_map>
#include <unordered_set>
#include "gpu/Resource.h"
#include "tgfx/utils/BytesKey.h"
#include "tgfx/utils/Clock.h"
#include "utils/Log.h"

namespace tgfx {
// Default maximum limit for the amount of GPU memory allocated to resources.
static const size_t DefaultMaxBytes = 96 * (1 << 20);  // 96MB

ResourceCache::ResourceCache(Context* context) : context(context), maxBytes(DefaultMaxBytes) {
}

bool ResourceCache::empty() const {
  return nonpurgeableResources.empty() && purgeableResources.empty();
}

void ResourceCache::releaseAll(bool releaseGPU) {
  for (auto& resource : nonpurgeableResources) {
    resource->release(releaseGPU);
  }
  nonpurgeableResources.clear();
  for (auto& resource : purgeableResources) {
    resource->release(releaseGPU);
  }
  purgeableResources.clear();
  recycleKeyMap.clear();
  resourceKeyMap.clear();
  purgeableBytes = 0;
  totalBytes = 0;
}

void ResourceCache::setCacheLimit(size_t bytesLimit) {
  if (maxBytes == bytesLimit) {
    return;
  }
  maxBytes = bytesLimit;
  purgeUntilMemoryTo(maxBytes);
}

std::shared_ptr<Resource> ResourceCache::findRecycledResource(const BytesKey& recycleKey) {
  auto result = recycleKeyMap.find(recycleKey);
  if (result == recycleKeyMap.end()) {
    return nullptr;
  }
  auto& list = result->second;
  size_t index = 0;
  bool found = false;
  for (auto& resource : list) {
    if (resource->isPurgeable() && !resource->hasExternalReferences()) {
      found = true;
      break;
    }
    index++;
  }
  if (!found) {
    return nullptr;
  }
  auto resource = list[index];
  return refResource(resource);
}

std::shared_ptr<Resource> ResourceCache::getResource(const ResourceKey& resourceKey) {
  auto resource = getUniqueResource(resourceKey);
  if (resource == nullptr) {
    return nullptr;
  }
  return refResource(resource);
}

bool ResourceCache::hasResource(const ResourceKey& resourceKey) {
  return getUniqueResource(resourceKey) != nullptr;
}

Resource* ResourceCache::getUniqueResource(const ResourceKey& resourceKey) {
  if (resourceKey.empty()) {
    return nullptr;
  }
  auto result = resourceKeyMap.find(resourceKey.domain());
  if (result == resourceKeyMap.end()) {
    return nullptr;
  }
  auto resource = result->second;
  if (!resource->hasExternalReferences()) {
    resourceKeyMap.erase(result);
    resource->resourceKey = {};
    return nullptr;
  }
  return resource;
}

void ResourceCache::AddToList(std::list<Resource*>& list, Resource* resource) {
  list.push_back(resource);
  resource->cachedList = &list;
  resource->cachedPosition = --list.end();
}

void ResourceCache::RemoveFromList(std::list<Resource*>& list, Resource* resource) {
  list.erase(resource->cachedPosition);
  resource->cachedList = nullptr;
}

bool ResourceCache::InList(const std::list<Resource*>& list, tgfx::Resource* resource) {
  return resource->cachedList == &list;
}

void ResourceCache::changeResourceKey(Resource* resource, const ResourceKey& resourceKey) {
  auto result = resourceKeyMap.find(resourceKey.domain());
  if (result != resourceKeyMap.end()) {
    result->second->removeResourceKey();
  }
  if (!resource->resourceKey.empty()) {
    resourceKeyMap.erase(resource->resourceKey.domain());
  }
  resource->resourceKey = resourceKey;
  resourceKeyMap[resourceKey.domain()] = resource;
}

void ResourceCache::removeResourceKey(Resource* resource) {
  resourceKeyMap.erase(resource->resourceKey.domain());
  resource->resourceKey = {};
}

std::shared_ptr<Resource> ResourceCache::addResource(Resource* resource,
                                                     const BytesKey& recycleKey) {
  resource->context = context;
  resource->recycleKey = recycleKey;
  if (resource->recycleKey.isValid()) {
    recycleKeyMap[resource->recycleKey].push_back(resource);
  }
  totalBytes += resource->memoryUsage();
  auto result = std::shared_ptr<Resource>(resource);
  // Add a strong reference to the resource itself, preventing it from being deleted by external
  // references.
  result->reference = result;
  AddToList(nonpurgeableResources, resource);
  return result;
}

std::shared_ptr<Resource> ResourceCache::refResource(Resource* resource) {
  if (InList(purgeableResources, resource)) {
    RemoveFromList(purgeableResources, resource);
    purgeableBytes -= resource->memoryUsage();
    AddToList(nonpurgeableResources, resource);
  }
  return resource->reference;
}

void ResourceCache::removeResource(Resource* resource) {
  if (!resource->resourceKey.empty()) {
    removeResourceKey(resource);
  }
  if (resource->recycleKey.isValid()) {
    auto result = recycleKeyMap.find(resource->recycleKey);
    if (result != recycleKeyMap.end()) {
      auto& list = result->second;
      list.erase(std::remove(list.begin(), list.end(), resource), list.end());
      if (list.empty()) {
        recycleKeyMap.erase(resource->recycleKey);
      }
    }
  }
  totalBytes -= resource->memoryUsage();
  resource->release(true);
}

void ResourceCache::purgeNotUsedSince(std::chrono::steady_clock::time_point purgeTime,
                                      bool recycledResourceOnly) {
  purgeResourcesByLRU(recycledResourceOnly,
                      [&](Resource* resource) { return resource->lastUsedTime >= purgeTime; });
}

bool ResourceCache::purgeUntilMemoryTo(size_t bytesLimit, bool recycledResourceOnly) {
  purgeResourcesByLRU(recycledResourceOnly, [&](Resource*) { return totalBytes <= bytesLimit; });
  return totalBytes <= bytesLimit;
}

bool ResourceCache::purgeToCacheLimit(std::chrono::steady_clock::time_point notUsedSinceTime) {
  purgeResourcesByLRU(false, [&](Resource* resource) {
    return resource->lastUsedTime >= notUsedSinceTime || totalBytes <= maxBytes;
  });
  return totalBytes <= maxBytes;
}

void ResourceCache::purgeResourcesByLRU(bool recycledResourceOnly,
                                        const std::function<bool(Resource*)>& satisfied) {
  processUnreferencedResources();
  auto item = purgeableResources.begin();
  while (item != purgeableResources.end()) {
    auto* resource = *item;
    if (satisfied(resource)) {
      break;
    }
    if (!recycledResourceOnly || !resource->hasExternalReferences()) {
      item = purgeableResources.erase(item);
      purgeableBytes -= resource->memoryUsage();
      removeResource(resource);
    } else {
      item++;
    }
  }
}

void ResourceCache::processUnreferencedResources() {
  std::vector<Resource*> needToPurge = {};
  for (auto& resource : nonpurgeableResources) {
    if (resource->isPurgeable()) {
      needToPurge.push_back(resource);
    }
  }
  if (needToPurge.empty()) {
    return;
  }
  auto currentTime = std::chrono::steady_clock::now();
  for (auto& resource : needToPurge) {
    RemoveFromList(nonpurgeableResources, resource);
    if (resource->recycleKey.isValid()) {
      AddToList(purgeableResources, resource);
      purgeableBytes += resource->memoryUsage();
      resource->lastUsedTime = currentTime;
    } else {
      removeResource(resource);
    }
  }
}
}  // namespace tgfx
