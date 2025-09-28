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

#include "core/utils/ReturnQueue.h"
#include "gpu/ResourceCache.h"
#include "gpu/resources/ResourceKey.h"

namespace tgfx {
/**
 * The base class for GPU resource. Note: Resource is not thread safe, do not access any properties
 * of a Resource unless its associated device is locked.
 */
class Resource : public ReturnNode {
 public:
  /**
   * A convenient method to add a resource to the cache.
   */
  template <class T>
  static std::shared_ptr<T> AddToCache(Context* context, T* resource,
                                       const ScratchKey& scratchKey = {}) {
    return std::static_pointer_cast<T>(context->resourceCache()->addResource(resource, scratchKey));
  }

  /**
   * A convenient method to retrieve a unique resource in the cache by the specified UniqueKey.
   */
  template <class T>
  static std::shared_ptr<T> Find(Context* context, const UniqueKey& uniqueKey) {
    return std::static_pointer_cast<T>(context->resourceCache()->findUniqueResource(uniqueKey));
  }

  /**
   * A convenient method to retrieve a scratch resource in the cache by the specified ScratchKey.
   */
  template <class T>
  static std::shared_ptr<T> Find(Context* context, const ScratchKey& scratchKey) {
    return std::static_pointer_cast<T>(context->resourceCache()->findScratchResource(scratchKey));
  }

  /**
   * Retrieves the context associated with this Resource.
   */
  Context* getContext() const {
    return context;
  }

  /**
   * Retrieves the amount of GPU memory used by this resource in bytes.
   */
  virtual size_t memoryUsage() const = 0;

  /**
   * Assigns a UniqueKey to the resource. The resource will be findable via this UniqueKey using
   * ResourceCache.findUniqueResource(). This method is not thread safe, call it only when the
   * associated context is locked.
   */
  void assignUniqueKey(const UniqueKey& newKey);

  /*
   * Removes the UniqueKey from the resource. This method is not thread safe, call it only when
   * the associated context is locked.
   */
  void removeUniqueKey();

 protected:
  Context* context = nullptr;
  std::weak_ptr<Resource> weakThis;

 private:
  ScratchKey scratchKey = {};
  UniqueKey uniqueKey = {};
  std::list<Resource*>* cachedList = nullptr;
  std::list<Resource*>::iterator cachedPosition;
  std::chrono::steady_clock::time_point lastUsedTime = {};

  bool isPurgeable() const {
    return weakThis.expired();
  }

  bool hasExternalReferences() const {
    return uniqueKey.useCount() > 1;
  }

  friend class ResourceCache;
};
}  // namespace tgfx
