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

#include "gpu/ResourceKey.h"

namespace tgfx {
/**
 * ResourceHandle holds a strong reference to the associated resource. When a resource is referenced
 * by a ResourceHandle, the Context ensures that the resource is not destroyed until all instances
 * of ResourceHandle are released or the Context itself is destroyed.
 */
class ResourceHandle {
 public:
  /**
   * Creates an empty ResourceHandle.
   */
  ResourceHandle() = default;

  explicit ResourceHandle(const ResourceKey& key);

  explicit ResourceHandle(ResourceKey&& key) noexcept;

  ResourceHandle(const ResourceHandle& key);

  ResourceHandle(ResourceHandle&& key) noexcept;

  virtual ~ResourceHandle();

  /**
   * Returns the ResourceKey associated with the ResourceHandle.
   */
  const ResourceKey& key() const {
    return resourceKey;
  }

  /**
   * Returns a global unique ID of the domain. Returns 0 if the ResourceHandle is empty.
   */
  uint32_t domain() const {
    return resourceKey.domain();
  }

  /**
   * Returns true if the ResourceHandle is empty.
   */
  bool empty() const {
    return resourceKey.empty();
  }

  /**
   * Returns the total number of times the domain has been referenced.
   */
  long useCount() const {
    return resourceKey.useCount();
  }

  /**
   * Returns the number of times the domain has been strongly referenced.
   */
  long strongCount() const {
    return resourceKey.strongCount();
  }

  ResourceHandle& operator=(const ResourceKey& key);

  ResourceHandle& operator=(ResourceKey&& key) noexcept;

  ResourceHandle& operator=(const ResourceHandle& handle);

  ResourceHandle& operator=(ResourceHandle&& handle) noexcept;

  bool operator==(const ResourceHandle& handle) const;

  bool operator!=(const ResourceHandle& handle) const;

 private:
  ResourceKey resourceKey = {};
};
}  // namespace tgfx
