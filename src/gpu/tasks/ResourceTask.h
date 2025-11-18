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

#include "core/utils/Log.h"
#include "gpu/proxies/ResourceProxy.h"
#include "gpu/resources/Resource.h"

namespace tgfx {
/**
 * The base class for all resource creation tasks.
 */
class ResourceTask {
 public:
  explicit ResourceTask(std::shared_ptr<ResourceProxy> proxy);

  virtual ~ResourceTask() = default;

  /**
   * Returns false if the resource creation is failed.
   */
  virtual bool execute(Context* context);

 protected:
  virtual std::shared_ptr<Resource> onMakeResource(Context* context) = 0;

 private:
  std::shared_ptr<ResourceProxy> proxy = nullptr;

  friend class DrawingManager;
};
}  // namespace tgfx
