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
class UniqueDomain;

/**
 * ResourceKey allows a code path to create cached resources for which it is the exclusive user.
 * The code path generates a unique domain which it sets on its keys. This guarantees that there are
 * no cross-domain collisions. Please refer to the comments in Resource::getResourceKey() for more
 * details.
 */
class ResourceKey {
 public:
  /**
   * Creates a weak ResourceKey that contains a valid domain and holds a weak reference to the
   * corresponding resource. When a resource is solely referenced by weak ResourceKeys, it falls
   * under the management of the Context and can be destroyed at any time.
   */
  static ResourceKey NewWeak();

  /**
   * Creates a strong ResourceKey that contains a valid domain and holds a strong reference to the
   * corresponding resource. When a resource is referenced by strong ResourceKeys, the Context
   * ensures that the resource is not destroyed until all strong ResourceKeys are released or the
   * Context itself is destroyed.
   */
  static ResourceKey NewStrong();

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
  uint64_t domain() const;

  /**
   * Returns true if the ResourceKey has no valid domain.
   */
  bool empty() const {
    return uniqueDomain == nullptr;
  }

  /**
   * Returns true if the ResourceKey holds a strong reference to the corresponding resource.
   */
  bool isStrong() const {
    return strong;
  }

  /**
   * Returns a strong ResourceKey that contains the same domain as the original key.
   */
  ResourceKey makeStrong() const;

  /**
   * Returns a weak ResourceKey that contains the same domain as the original key.
   */
  ResourceKey makeWeak() const;

  /**
   * Returns the total number of times the domain has been referenced.
   */
  long useCount() const;

  /**
   * Returns the number of times the domain has been referenced strongly.
   */
  long strongCount() const;

  ResourceKey& operator=(const ResourceKey& key);

  ResourceKey& operator=(ResourceKey&& key) noexcept;

 private:
  UniqueDomain* uniqueDomain = nullptr;
  bool strong = false;

  ResourceKey(UniqueDomain* block, bool strong);
};
}  // namespace tgfx
