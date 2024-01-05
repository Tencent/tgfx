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

#include "tgfx/gpu/ResourceCache.h"
#include <unordered_map>
#include <unordered_set>
#include "tgfx/gpu/Resource.h"
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
  scratchKeyMap.clear();
  uniqueKeyMap.clear();
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

std::shared_ptr<Resource> ResourceCache::findScratchResource(const BytesKey& scratchKey) {
  auto result = scratchKeyMap.find(scratchKey);
  if (result == scratchKeyMap.end()) {
    return nullptr;
  }
  auto& list = result->second;
  int index = 0;
  bool found = false;
  for (auto& resource : list) {
    if (resource->isPurgeable() && !resource->hasValidUniqueKey()) {
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

std::shared_ptr<Resource> ResourceCache::findUniqueResource(const UniqueKey& uniqueKey) {
  auto resource = getUniqueResource(uniqueKey);
  if (resource == nullptr) {
    return nullptr;
  }
  return refResource(resource);
}

bool ResourceCache::hasUniqueResource(const UniqueKey& uniqueKey) {
  return getUniqueResource(uniqueKey) != nullptr;
}

Resource* ResourceCache::getUniqueResource(const UniqueKey& uniqueKey) {
  if (uniqueKey.empty()) {
    return nullptr;
  }
  auto result = uniqueKeyMap.find(uniqueKey.uniqueID());
  if (result == uniqueKeyMap.end()) {
    return nullptr;
  }
  auto resource = result->second;
  if (!resource->hasValidUniqueKey()) {
    uniqueKeyMap.erase(result);
    resource->uniqueKey = {};
    resource->uniqueKeyGeneration = 0;
    return nullptr;
  }
  return resource;
}

void ResourceCache::AddToList(std::list<Resource*>& list, Resource* resource) {
  list.push_front(resource);
  resource->cachedList = &list;
  resource->cachedPosition = list.begin();
}

void ResourceCache::RemoveFromList(std::list<Resource*>& list, Resource* resource) {
  list.erase(resource->cachedPosition);
  resource->cachedList = nullptr;
}

bool ResourceCache::InList(const std::list<Resource*>& list, tgfx::Resource* resource) {
  return resource->cachedList == &list;
}

void ResourceCache::changeUniqueKey(Resource* resource, const UniqueKey& uniqueKey) {
  auto result = uniqueKeyMap.find(uniqueKey.uniqueID());
  if (result != uniqueKeyMap.end()) {
    result->second->removeUniqueKey();
  }
  if (!resource->uniqueKey.empty()) {
    uniqueKeyMap.erase(resource->uniqueKey.uniqueID());
  }
  resource->uniqueKey = uniqueKey;
  resource->uniqueKeyGeneration = uniqueKey.uniqueID();
  uniqueKeyMap[uniqueKey.uniqueID()] = resource;
}

void ResourceCache::removeUniqueKey(Resource* resource) {
  uniqueKeyMap.erase(resource->uniqueKey.uniqueID());
  resource->uniqueKey = {};
  resource->uniqueKeyGeneration = 0;
}

std::shared_ptr<Resource> ResourceCache::refResource(Resource* resource) {
  if (InList(purgeableResources, resource)) {
    RemoveFromList(purgeableResources, resource);
    purgeableBytes -= resource->memoryUsage();
    AddToList(nonpurgeableResources, resource);
  }
  return resource->reference;
}

std::shared_ptr<Resource> ResourceCache::addResource(Resource* resource) {
  if (resource->scratchKey.isValid()) {
    scratchKeyMap[resource->scratchKey].push_back(resource);
  }
  totalBytes += resource->memoryUsage();
  auto result = std::shared_ptr<Resource>(resource);
  // Add a strong reference to the resource itself, preventing it from being deleted by external
  // references.
  result->reference = result;
  AddToList(nonpurgeableResources, resource);
  return result;
}

void ResourceCache::removeResource(Resource* resource) {
  if (!resource->uniqueKey.empty()) {
    removeUniqueKey(resource);
  }
  if (resource->scratchKey.isValid()) {
    auto result = scratchKeyMap.find(resource->scratchKey);
    if (result != scratchKeyMap.end()) {
      auto& list = result->second;
      list.erase(std::remove(list.begin(), list.end(), resource), list.end());
      if (list.empty()) {
        scratchKeyMap.erase(resource->scratchKey);
      }
    }
  }
  totalBytes -= resource->memoryUsage();
  resource->release(true);
}

void ResourceCache::purgeNotUsedSince(int64_t purgeTime, bool scratchResourcesOnly) {
  purgeResourcesByLRU(scratchResourcesOnly,
                      [&](Resource* resource) { return resource->lastUsedTime >= purgeTime; });
}

bool ResourceCache::purgeUntilMemoryTo(size_t bytesLimit, bool scratchResourcesOnly) {
  purgeResourcesByLRU(scratchResourcesOnly, [&](Resource*) { return totalBytes <= bytesLimit; });
  return totalBytes <= bytesLimit;
}

void ResourceCache::purgeResourcesByLRU(bool scratchResourcesOnly,
                                        const std::function<bool(Resource*)>& satisfied) {
  processUnreferencedResources();
  std::vector<Resource*> needToPurge = {};
  for (auto item = purgeableResources.rbegin(); item != purgeableResources.rend(); item++) {
    auto* resource = *item;
    if (satisfied(resource)) {
      break;
    }
    if (!scratchResourcesOnly || !resource->hasValidUniqueKey()) {
      needToPurge.push_back(resource);
    }
  }
  for (auto& resource : needToPurge) {
    RemoveFromList(purgeableResources, resource);
    purgeableBytes -= resource->memoryUsage();
    removeResource(resource);
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
  for (auto& resource : needToPurge) {
    RemoveFromList(nonpurgeableResources, resource);
    if (resource->scratchKey.isValid()) {
      AddToList(purgeableResources, resource);
      purgeableBytes += resource->memoryUsage();
      resource->lastUsedTime = Clock::Now();
    } else {
      removeResource(resource);
    }
  }
}
}  // namespace tgfx
