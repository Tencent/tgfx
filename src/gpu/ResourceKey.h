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

#include <memory>
#include "tgfx/utils/BytesKey.h"

namespace tgfx {
/**
 * A key used for scratch resources. There are three important rules about scratch keys:
 *
 *    1) Multiple resources can share the same scratch key. Therefore, resources assigned the same
 *       scratch key should be interchangeable with respect to the code that uses them.
 *    2) A resource can have at most one scratch key, and it is set at resource creation by the
 *       resource itself.
 *    3) When a scratch resource is referenced, it will not be returned from the cache for a
 *       subsequent cache request until all refs are released. This facilitates using a scratch key
 *       for multiple render-to-texture scenarios.
 */
using ScratchKey = BytesKey;

template <typename T>
using ScratchKeyMap = BytesKeyMap<T>;

class UniqueDomain;

/**
 * A key that allows for exclusive use of a resource for a use case (AKA "domain"). There are three
 * rules governing the use of unique keys:
 *
 *    1) Only one resource can have a given unique key at a time. Hence, "unique".
 *    2) A resource can have at most one unique key at a time.
 *    3) Unlike scratch keys, multiple requests for a unique key will return the same resource even
 *       if the resource already has refs.
 *
 * This key type allows a code path to create cached resources for which it is the exclusive user.
 * The code path creates a domain which it sets on its keys. This guarantees that there are no
 * cross-domain collisions. Unique keys preempt scratch keys. While a resource has a valid unique
 * key, it is inaccessible via its scratch key. It can become scratch again if the unique key is
 * removed or no longer has any external references.
 */
class UniqueKey {
 public:
  /**
   * Creates a weak UniqueKey that contains a valid domain and holds a weak reference to the
   * corresponding resource. When a resource is solely referenced by weak unique keys, it falls
   * under the management of the Context and can be destroyed at any time.
   */
  static UniqueKey MakeWeak();

  /**
   * Creates a strong UniqueKey that contains a valid domain and holds a strong reference to the
   * corresponding resource. When a resource is referenced by strong unique keys, the Context
   * ensures that the resource is not destroyed until all strong unique keys are released or the
   * Context itself is destroyed.
   */
  static UniqueKey MakeStrong();

  /**
   * Creates an empty UniqueKey.
   */
  UniqueKey() = default;

  UniqueKey(const UniqueKey& key);

  UniqueKey(UniqueKey&& key) noexcept;

  virtual ~UniqueKey();

  /**
   * Returns a global unique ID of the domain. Returns 0 if the UniqueKey is empty.
   */
  uint32_t domainID() const;

  /**
   * Returns true if the UniqueKey has no valid domain.
   */
  bool empty() const {
    return uniqueDomain == nullptr;
  }

  /**
   * Returns true if the UniqueKey holds a strong reference to the corresponding resource.
   */
  bool isStrong() const {
    return strong;
  }

  /**
   * Returns a strong UniqueKey that contains the same domain as the original key.
   */
  UniqueKey makeStrong() const;

  /**
   * Returns a weak UniqueKey that contains the same domain as the original key.
   */
  UniqueKey makeWeak() const;

  /**
   * Returns the total number of times the domain has been referenced.
   */
  long useCount() const;

  /**
   * Returns the number of times the domain has been referenced strongly.
   */
  long strongCount() const;

  UniqueKey& operator=(const UniqueKey& key);

  UniqueKey& operator=(UniqueKey&& key) noexcept;

 private:
  UniqueDomain* uniqueDomain = nullptr;
  bool strong = false;

  UniqueKey(UniqueDomain* block, bool strong);
};
}  // namespace tgfx
