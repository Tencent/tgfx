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

#include "gpu/ResourceCache.h"
#include "gpu/ResourceKey.h"

namespace tgfx {
/**
 * The base class for GPU resource. Overrides the onReleaseGPU() method to free all GPU resources.
 * No backend API calls should be made during destructuring since there may be no GPU context that
 * is current on the calling thread. Note: Resource is not thread safe, do not access any properties
 * of a Resource unless its associated device is locked.
 */
class Resource {
 public:
  /**
   * A convenient method to add a resource to the cache.
   */
  template <class T>
  static std::shared_ptr<T> AddToCache(Context* context, T* resource,
                                       const BytesKey& recycleKey = {}) {
    return std::static_pointer_cast<T>(context->resourceCache()->addResource(resource, recycleKey));
  }

  /**
   * A convenient method to retrieve the corresponding resource in the cache by the specified
   * ResourceKey.
   */
  template <class T>
  static std::shared_ptr<T> Get(Context* context, const ResourceKey& resourceKey) {
    return std::static_pointer_cast<T>(context->resourceCache()->getResource(resourceKey));
  }

  /**
   * A convenient method to retrieve a recycled resource in the cache by the specified recycle key.
   */
  template <class T>
  static std::shared_ptr<T> FindRecycled(Context* context, const BytesKey& recycleKey) {
    return std::static_pointer_cast<T>(context->resourceCache()->findRecycledResource(recycleKey));
  }

  virtual ~Resource() = default;

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
   * Returns the associated recycle key. There are three important rules about recycle keys:
   *
   *    1) Multiple resources can share the same recycle key. Therefore, resources assigned the same
   *       recycle key should be interchangeable with respect to the code that uses them.
   *    2) A resource can have at most one recycle key, and it is set at resource creation by the
   *       resource itself.
   *    3) When a recycled resource is referenced, it will not be returned from the cache for a
   *       subsequent cache request until all refs are released.
   */
  const BytesKey getRecycleKey() const {
    return recycleKey;
  }

  /**
   * Returns the associated ResourceKey. There are three rules governing the use of ResourceKeys:
   *
   *    1) Only one resource can have a given ResourceKey at a time.
   *    2) A resource can have at most one ResourceKey at a time.
   *    3) Unlike recycle keys, multiple requests for a ResourceKey will return the same resource
   *       even if the resource already has refs.
   *
   * ResourceKeys preempt recycle keys. While a resource has a valid ResourceKey, it is inaccessible
   * via its recycle key. It can become recyclable again if the ResourceKey is removed or no longer
   * has any external references.
   */
  const ResourceKey& getResourceKey() const {
    return resourceKey;
  }

  /**
   * Assigns a ResourceKey to the resource. The resource will be findable via this ResourceKey using
   * ResourceCache.getResource(). This method is not thread safe, call it only when the
   * associated context is locked.
   */
  void assignResourceKey(const ResourceKey& newKey);

  /*
   * Removes the ResourceKey from the resource. This method is not thread safe, call it only when
   * the associated context is locked.
   */
  void removeResourceKey();

 protected:
  Context* context = nullptr;

  /**
   * Overridden to free GPU resources in the backend API.
   */
  virtual void onReleaseGPU() = 0;

 private:
  std::shared_ptr<Resource> reference;
  BytesKey recycleKey = {};
  ResourceKey resourceKey = {};
  std::list<Resource*>* cachedList = nullptr;
  std::list<Resource*>::iterator cachedPosition;
  int64_t lastUsedTime = 0;

  virtual bool isPurgeable() const {
    return reference.use_count() <= 1 && resourceKey.strongCount() == 0;
  }

  bool hasExternalReferences() const {
    return resourceKey.useCount() > 1;
  }

  void release(bool releaseGPU);

  friend class ResourceCache;
};
}  // namespace tgfx
