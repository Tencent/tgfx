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

#include "ResourceTask.h"

namespace tgfx {
ResourceTask::ResourceTask(std::shared_ptr<ResourceProxy> proxy) : proxy(std::move(proxy)) {
}

bool ResourceTask::execute(Context* context) {
  if (proxy.use_count() == 1) {
    // Skip resource creation if no external proxy needs it.
    return false;
  }
  auto resource = onMakeResource(context);
  if (resource == nullptr) {
    return false;
  }
  resource->assignUniqueKey(uniqueKey);
  proxy->resource = std::move(resource);
  return true;
}
}  // namespace tgfx
