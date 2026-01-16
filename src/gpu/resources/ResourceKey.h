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

#include <atomic>
#include <cstring>
#include <memory>
#include "tgfx/core/BytesKey.h"

namespace tgfx {
/**
 * Base class for all gpu Resource cache keys. There are two types of cache keys. Refer to the
 * comments for each key type below.
 */
class ResourceKey {
 public:
  ResourceKey() = default;

  ResourceKey(const ResourceKey& that);

  virtual ~ResourceKey();

  /**
   * Returns true if the resource key is empty.
   */
  bool empty() const {
    return data == nullptr;
  }

  /**
   * Returns the hash of the key. This is used to quickly compare keys.
   */
  size_t hash() const {
    return data == nullptr ? 0 : data[0];
  }

  ResourceKey& operator=(const ResourceKey& that);

  bool operator==(const ResourceKey& that) const;

  bool operator!=(const ResourceKey& that) const {
    return !(*this == that);
  }

 protected:
  ResourceKey(uint32_t* data, size_t count);

  ResourceKey(ResourceKey&& key) noexcept;

  uint32_t* data = nullptr;
  size_t count = 0;
};

struct ResourceKeyHasher {
  size_t operator()(const ResourceKey& key) const {
    return key.hash();
  }
};

template <typename T>
using ResourceKeyMap = std::unordered_map<ResourceKey, T, ResourceKeyHasher>;

/**
 * A key used for scratch resources. There are three important rules about scratch keys:
 *
 *    1) Multiple resources can share the same scratch key. Therefore, resources assigned the same
 *       scratch key should be interchangeable with respect to the code that uses them.
 *    2) A resource can have at most one scratch key, and it is set at resource creation by the
 *       resource itself.
 *    3) When a scratch resource is referenced, it will not be returned from the cache for a
 *       subsequent cache request until all references are released.
 */
class ScratchKey : public ResourceKey {
 public:
  /**
   * Creates an empty ScratchKey.
   */
  ScratchKey() = default;

  ScratchKey(const ScratchKey& that) : ResourceKey(that) {
  }

  ScratchKey(ScratchKey&& that) noexcept : ResourceKey(std::move(that)) {
  }

  ScratchKey(const BytesKey& that) {
    *this = that;
  }

  ScratchKey& operator=(const ScratchKey& that) {
    ResourceKey::operator=(that);
    return *this;
  }

  ScratchKey& operator=(const BytesKey& that);

  bool operator==(const ScratchKey& that) const {
    return ResourceKey::operator==(that);
  }

  bool operator!=(const ScratchKey& that) const {
    return !(*this == that);
  }

 private:
  ScratchKey(uint32_t* data, size_t count);
};

class UniqueDomain;

/**
 * A key that allows for exclusive use of a resource for a use case (AKA "domain"). There are three
 * rules governing the use of unique keys:
 *
 *    1) Only one resource can have a given unique key at a time. Hence, "unique".
 *    2) A resource can have at most one unique key at a time.
 *    3) Unlike scratch keys, multiple requests for a unique key will return the same resource even
 *       if the resource already has references.
 *
 * This key type allows a code path to create cached resources for which it is the exclusive user.
 * The code path creates a domain which it sets on its keys. This guarantees that there are no
 * cross-domain collisions. Unique keys preempt scratch keys. While a resource has a unique key, it
 * is inaccessible via its scratch key. It can become scratch again if the unique key is removed or
 * no longer has any external references.
 */
class UniqueKey : public ResourceKey {
 public:
  /**
   * Creates a new UniqueKey with a valid domain.
   */
  static UniqueKey Make();

  /**
   * Creates a new UniqueKey by appending a list of uint32_t values to the given UniqueKey. The
   * returned UniqueKey will share the same unique domain as the original UniqueKey. Returns the
   * original UniqueKey if the data is empty.
   */
  static UniqueKey Append(const UniqueKey& uniqueKey, const uint32_t* data, size_t count);

  /**
   * Creates an empty UniqueKey.
   */
  UniqueKey() = default;

  UniqueKey(const UniqueKey& key);

  UniqueKey(UniqueKey&& key) noexcept;

  virtual ~UniqueKey();

  /**
   * Returns the domain ID of the unique key.
   */
  uint32_t domainID() const;

  /**
   * Returns the total number of times the domain has been referenced.
   */
  long useCount() const;

  UniqueKey& operator=(const UniqueKey& key);

  UniqueKey& operator=(UniqueKey&& key) noexcept;

  bool operator==(const UniqueKey& that) const {
    return ResourceKey::operator==(that);
  }

  bool operator!=(const UniqueKey& that) const {
    return !(*this == that);
  }

 private:
  UniqueDomain* uniqueDomain = nullptr;

  explicit UniqueKey(UniqueDomain* domain);

  UniqueKey(uint32_t* data, size_t count, UniqueDomain* domain);

  friend class ResourceHandle;
  friend class LazyUniqueKey;
};

/**
 * LazyUniqueKey defers the acquisition of the UniqueKey until it is actually needed.
 */
class LazyUniqueKey {
 public:
  ~LazyUniqueKey();

  /**
   * Returns the associated UniqueKey. If the UniqueKey is empty, it will create a new one
   * immediately. Calling this method from multiple threads will not create multiple UniqueKeys.
   * This method is thread-safe as long as there is no concurrent reset() call.
   */
  UniqueKey get() const;

  /**
   * Resets the LazyUniqueKey to an empty state. This method is not thread-safe.
   */
  void reset();

 private:
  mutable std::atomic<UniqueDomain*> uniqueDomain = nullptr;
};
}  // namespace tgfx
