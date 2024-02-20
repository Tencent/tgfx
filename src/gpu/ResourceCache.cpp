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
#include "gpu/Resource.h"

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

std::shared_ptr<Resource> ResourceCache::findScratchResource(const ScratchKey& scratchKey) {
  auto result = scratchKeyMap.find(scratchKey);
  if (result == scratchKeyMap.end()) {
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
  auto result = uniqueKeyMap.find(uniqueKey.domain());
  if (result == uniqueKeyMap.end()) {
    return nullptr;
  }
  auto resource = result->second;
  if (!resource->hasExternalReferences()) {
    uniqueKeyMap.erase(result);
    resource->uniqueKey = {};
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

void ResourceCache::changeUniqueKey(Resource* resource, const UniqueKey& uniqueKey) {
  auto result = uniqueKeyMap.find(uniqueKey.domain());
  if (result != uniqueKeyMap.end()) {
    result->second->removeUniqueKey();
  }
  if (!resource->uniqueKey.empty()) {
    uniqueKeyMap.erase(resource->uniqueKey.domain());
  }
  resource->uniqueKey = uniqueKey;
  uniqueKeyMap[uniqueKey.domain()] = resource;
}

void ResourceCache::removeUniqueKey(Resource* resource) {
  uniqueKeyMap.erase(resource->uniqueKey.domain());
  resource->uniqueKey = {};
}

std::shared_ptr<Resource> ResourceCache::addResource(Resource* resource,
                                                     const ScratchKey& scratchKey) {
  resource->context = context;
  resource->scratchKey = scratchKey;
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

std::shared_ptr<Resource> ResourceCache::refResource(Resource* resource) {
  if (InList(purgeableResources, resource)) {
    RemoveFromList(purgeableResources, resource);
    purgeableBytes -= resource->memoryUsage();
    AddToList(nonpurgeableResources, resource);
  }
  return resource->reference;
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

void ResourceCache::purgeNotUsedSince(std::chrono::steady_clock::time_point purgeTime,
                                      bool scratchResourcesOnly) {
  purgeResourcesByLRU(scratchResourcesOnly,
                      [&](Resource* resource) { return resource->lastUsedTime >= purgeTime; });
}

bool ResourceCache::purgeUntilMemoryTo(size_t bytesLimit, bool scratchResourcesOnly) {
  purgeResourcesByLRU(scratchResourcesOnly, [&](Resource*) { return totalBytes <= bytesLimit; });
  return totalBytes <= bytesLimit;
}

bool ResourceCache::purgeToCacheLimit(std::chrono::steady_clock::time_point notUsedSinceTime) {
  purgeResourcesByLRU(false, [&](Resource* resource) {
    return resource->lastUsedTime >= notUsedSinceTime || totalBytes <= maxBytes;
  });
  return totalBytes <= maxBytes;
}

void ResourceCache::purgeResourcesByLRU(bool scratchResourceOnly,
                                        const std::function<bool(Resource*)>& satisfied) {
  processUnreferencedResources();
  auto item = purgeableResources.begin();
  while (item != purgeableResources.end()) {
    auto* resource = *item;
    if (satisfied(resource)) {
      break;
    }
    if (!scratchResourceOnly || !resource->hasExternalReferences()) {
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
    if (resource->scratchKey.isValid()) {
      AddToList(purgeableResources, resource);
      purgeableBytes += resource->memoryUsage();
      resource->lastUsedTime = currentTime;
    } else {
      removeResource(resource);
    }
  }
}
}  // namespace tgfx
