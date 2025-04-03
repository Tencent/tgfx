/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "AtlasBufferUploadTask.h"

namespace tgfx {
AtlasBufferUploadTask::AtlasBufferUploadTask(UniqueKey atlasKey,
                                             std::unique_ptr<DataSource<AtlasBuffer>> source)
    : ResourceTask(std::move(atlasKey)), source(std::move(source)) {
}

bool AtlasBufferUploadTask::execute(Context*) {
  if (uniqueKey.strongCount() <= 0) {
    // Skip the resource creation if there is no proxy is referencing it.
    return false;
  }

  if (source == nullptr) {
    return false;
  }

  auto atlasBuffer = source->getData();
  if (atlasBuffer == nullptr) {
    return false;
  }

  //TODO upload atlas
  source = nullptr;
  return true;
}

}  //namespace tgfx