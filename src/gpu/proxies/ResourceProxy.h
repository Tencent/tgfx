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

#include "gpu/Resource.h"

namespace tgfx {
/**
 * The base class for all proxy-derived objects. It delays the acquisition of resources until they
 * are actually required.
 */
class ResourceProxy {
 public:
  virtual ~ResourceProxy() = default;

  /**
   * Retrieves the context associated with this ResourceProxy.
   */
  Context* getContext() const {
    return context;
  }

  /**
   * Returns the ResourceKey associated with this ResourceProxy.
   */
  const ResourceKey& getResourceKey() const {
    return resourceKey;
  }

 protected:
  Context* context = nullptr;
  ResourceKey resourceKey = {};

  ResourceProxy() = default;

  friend class ProxyProvider;
};
}  // namespace tgfx
