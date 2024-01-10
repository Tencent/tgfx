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

#include "ResourceTask.h"
#include "utils/Log.h"

namespace tgfx {
ResourceTask::ResourceTask(UniqueKey uniqueKey) : uniqueKey(std::move(uniqueKey)) {
}

bool ResourceTask::execute(Context* context) {
  if (uniqueKey.strongCount() <= 1) {
    // Skip the resource creation if there is no proxy is referencing it.
    return false;
  }
  auto resource = onMakeResource(context);
  if (resource == nullptr) {
    LOGE("ResourceTask::execute() Failed to create resource!");
    return false;
  }
  resource->assignUniqueKey(uniqueKey);
  return true;
}
}  // namespace tgfx
