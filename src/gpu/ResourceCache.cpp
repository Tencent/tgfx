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

#include "gpu/ResourceCache.h"
#include <unordered_map>
#include "../../include/tgfx/core/Log.h"
#include "gpu/resources/Resource.h"

namespace tgfx {
static constexpr size_t MAX_EXPIRATION_FRAMES = 1000000;  // About 4.5 hours at 60 FPS
static constexpr size_t SCRATCH_EXPIRATION_FRAMES = 2;

ResourceCache::ResourceCache(Context* context) : context(context) {
}

ResourceCache::~ResourceCache() {
  releaseAll();
}

bool ResourceCache::empty() const {
  return nonpurgeableResources.empty() && purgeableResources.empty();
}

void ResourceCache::setCacheLimit(size_t bytesLimit) {
  if (maxBytes == bytesLimit) {
    return;
  }
  maxBytes = bytesLimit;
  purgeAsNeeded();
}

void ResourceCache::setExpirationFrames(size_t frames) {
  if (frames > MAX_EXPIRATION_FRAMES) {
    frames = MAX_EXPIRATION_FRAMES;
  }
  if (_expirationFrames == frames) {
    return;
  }
  _expirationFrames = frames;
  while (frameTimes.size() > _expirationFrames + 1) {
    frameTimes.pop_front();
  }
  purgeAsNeeded();
}

void ResourceCache::purgeNotUsedSince(std::chrono::steady_clock::time_point purgeTime) {
  processUnreferencedResources();
  purgeResourcesByLRU(false,
                      [&](Resource* resource) { return resource->lastUsedTime > purgeTime; });
}

bool ResourceCache::purgeUntilMemoryTo(size_t bytesLimit) {
  processUnreferencedResources();
  purgeResourcesByLRU(false, [&](Resource*) { return totalBytes <= bytesLimit; });
  return totalBytes <= bytesLimit;
}

void ResourceCache::advanceFrameAndPurge() {
  currentFrameTime = std::chrono::steady_clock::now();
  frameTimes.push_back(currentFrameTime);
  if (frameTimes.size() > _expirationFrames + 1) {
    frameTimes.pop_front();
  }
  purgeAsNeeded();
}

void ResourceCache::purgeAsNeeded() {
  processUnreferencedResources();
  if (frameTimes.size() > _expirationFrames) {
    auto purgeTime = frameTimes[frameTimes.size() - _expirationFrames - 1];
    purgeResourcesByLRU(false, [&](Resource* resource) {
      return resource->lastUsedTime > purgeTime && totalBytes <= maxBytes;
    });
  } else {
    purgeResourcesByLRU(false, [&](Resource*) { return totalBytes <= maxBytes; });
  }
  if (frameTimes.size() > SCRATCH_EXPIRATION_FRAMES) {
    auto purgeTime = frameTimes[frameTimes.size() - SCRATCH_EXPIRATION_FRAMES - 1];
    purgeResourcesByLRU(true,
                        [&](Resource* resource) { return resource->lastUsedTime > purgeTime; });
  }
}

void ResourceCache::purgeResourcesByLRU(bool scratchResourceOnly,
                                        const std::function<bool(Resource*)>& satisfied) {
  auto item = purgeableResources.begin();
  while (item != purgeableResources.end()) {
    auto resource = *item;
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
  while (auto resource = static_cast<Resource*>(returnQueue->dequeue())) {
    DEBUG_ASSERT(resource->isPurgeable());
    RemoveFromList(nonpurgeableResources, resource);
    if (!resource->scratchKey.empty() || resource->hasExternalReferences()) {
      AddToList(purgeableResources, resource);
      purgeableBytes += resource->memoryUsage();
      resource->lastUsedTime = currentFrameTime;
    } else {
      removeResource(resource);
    }
  }
}

void ResourceCache::releaseAll() {
  for (auto& resource : nonpurgeableResources) {
    // Note that we don't delete the resource here, because it may still be referenced externally.
    resource->context = nullptr;
  }
  nonpurgeableResources.clear();
  for (auto& resource : purgeableResources) {
    delete resource;
  }
  purgeableResources.clear();
  returnQueue = ReturnQueue::Make();
  scratchKeyMap.clear();
  uniqueKeyMap.clear();
  purgeableBytes = 0;
  totalBytes = 0;
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
  auto result = uniqueKeyMap.find(uniqueKey);
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

void ResourceCache::changeUniqueKey(Resource* resource, const UniqueKey& uniqueKey) {
  auto result = uniqueKeyMap.find(uniqueKey);
  if (result != uniqueKeyMap.end()) {
    result->second->removeUniqueKey();
  }
  if (!resource->uniqueKey.empty()) {
    uniqueKeyMap.erase(resource->uniqueKey);
  }
  resource->uniqueKey = uniqueKey;
  uniqueKeyMap[uniqueKey] = resource;
}

void ResourceCache::removeUniqueKey(Resource* resource) {
  uniqueKeyMap.erase(resource->uniqueKey);
  resource->uniqueKey = {};
}

std::shared_ptr<Resource> ResourceCache::addResource(Resource* resource,
                                                     const ScratchKey& scratchKey) {
  resource->context = context;
  resource->scratchKey = scratchKey;
  if (!resource->scratchKey.empty()) {
    scratchKeyMap[resource->scratchKey].push_back(resource);
  }
  totalBytes += resource->memoryUsage();
  AddToList(nonpurgeableResources, resource);
  auto reference = std::static_pointer_cast<Resource>(returnQueue->makeShared(resource));
  reference->weakThis = reference;
  return reference;
}

std::shared_ptr<Resource> ResourceCache::refResource(Resource* resource) {
  if (auto reference = resource->weakThis.lock()) {
    return reference;
  }
  processUnreferencedResources();
  DEBUG_ASSERT(resource->cachedList == &purgeableResources);
  RemoveFromList(purgeableResources, resource);
  purgeableBytes -= resource->memoryUsage();
  AddToList(nonpurgeableResources, resource);
  auto reference = std::static_pointer_cast<Resource>(returnQueue->makeShared(resource));
  reference->weakThis = reference;
  return reference;
}

void ResourceCache::removeResource(Resource* resource) {
  if (!resource->uniqueKey.empty()) {
    removeUniqueKey(resource);
  }
  if (!resource->scratchKey.empty()) {
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
  delete resource;
}
}  // namespace tgfx
