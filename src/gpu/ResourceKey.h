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

#include <atomic>
#include <memory>
#include "tgfx/utils/BytesKey.h"

namespace tgfx {
class UniqueDomain;

/**
 * ResourceKey allows a code path to create cached resources for which it is the exclusive user.
 * The code path generates a unique domain which it sets on its keys. This guarantees that there are
 * no cross-domain collisions. When a resource is only referenced by ResourceKeys, it falls under
 * the management of the Context and can be destroyed at any time. To maintain a strong reference to
 * the resource, use the ResourceHandle class. For further details on the differences between
 * ResourceKeys and recycle keys, please refer to the comments in Resource::getResourceKey() and
 * Resource::getRecycleKey().
 */
class ResourceKey {
 public:
  /**
   * Creates a new ResourceKey with a valid domain.
   */
  static ResourceKey Make();

  /**
   * Creates an empty ResourceKey.
   */
  ResourceKey() = default;

  ResourceKey(const ResourceKey& key);

  ResourceKey(ResourceKey&& key) noexcept;

  virtual ~ResourceKey();

  /**
   * Returns a global unique ID of the domain. Returns 0 if the ResourceKey is empty.
   */
  uint32_t domain() const;

  /**
   * Returns true if the ResourceKey has no valid domain.
   */
  bool empty() const {
    return uniqueDomain == nullptr;
  }

  /**
   * Returns the total number of times the domain has been referenced.
   */
  long useCount() const;

  /**
   * Returns the number of times the domain has been strongly referenced.
   */
  long strongCount() const;

  ResourceKey& operator=(const ResourceKey& key);

  ResourceKey& operator=(ResourceKey&& key) noexcept;

  bool operator==(const ResourceKey& key) const;

  bool operator!=(const ResourceKey& key) const;

 private:
  UniqueDomain* uniqueDomain = nullptr;

  explicit ResourceKey(UniqueDomain* block);

  void addStrong();

  void releaseStrong();

  friend class ResourceHandle;
  friend class LazyResourceKey;
};

/**
 * LazyResourceKey defers the acquisition of the ResourceKey until it is actually needed.
 */
class LazyResourceKey {
 public:
  ~LazyResourceKey();

  /**
   * Returns the associated ResourceKey. If the ResourceKey is empty, it will create a new one
   * immediately. Calling this method from multiple threads will not create multiple ResourceKeys.
   * This method is thread-safe as long as there is no concurrent reset() call.
   */
  ResourceKey get();

  /**
   * Resets the LazyResourceKey to an empty state. This method is not thread-safe.
   */
  void reset();

 private:
  std::atomic<UniqueDomain*> uniqueDomain = nullptr;
};
}  // namespace tgfx
