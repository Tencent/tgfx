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

#include "../../../include/tgfx/core/Log.h"
#include "gpu/resources/Resource.h"

namespace tgfx {
/**
 * The base class for all proxy-derived objects. It defers the acquisition of resources until they
 * are actually required.
 */
class ResourceProxy {
 public:
  virtual ~ResourceProxy() = default;

  /**
   * Retrieves the context associated with this ResourceProxy.
   */
  virtual Context* getContext() const {
    return context;
  }

  void assignUniqueKey(const UniqueKey& key) {
    uniqueKey = key;
    if (resource != nullptr) {
      resource->assignUniqueKey(key);
    }
  }

 protected:
  Context* context = nullptr;
  mutable std::shared_ptr<Resource> resource = nullptr;
  UniqueKey uniqueKey;

  explicit ResourceProxy(std::shared_ptr<Resource> resource) : resource(std::move(resource)) {
  }

  ResourceProxy() = default;

  friend class ResourceTask;
  friend class ShapeBufferUploadTask;
  friend class ProxyProvider;
};
}  // namespace tgfx
