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
#include "core/utils/UniqueID.h"
#include "gpu/GpuBuffer.h"

namespace tgfx {
AtlasBufferUploadTask::AtlasBufferUploadTask(UniqueKey atlasKey,
                                             std::vector<std::pair<UniqueKey, UniqueKey>> proxyKeys,
                                             std::unique_ptr<DataSource<AtlasBuffer>> source)
    : ResourceTask(std::move(atlasKey)), source(std::move(source)),
      atlasProxyKeys(std::move(proxyKeys)) {
}

bool AtlasBufferUploadTask::execute(Context* context) {
  if (uniqueKey.strongCount() <= 0) {
    return false;
  }

  if (source == nullptr) {
    return false;
  }

  auto atlasBuffer = source->getData();
  if (atlasBuffer == nullptr) {
    return false;
  }

  const auto& geometryData = atlasBuffer->geometryData();
  if (atlasProxyKeys.size() != geometryData.size()) {
    return false;
  }

  size_t index = 0;
  for (const auto& geometry : geometryData) {
    auto vertexBuffer = GpuBuffer::Make(context, BufferType::Vertex, geometry.vertices->data(),
                                        geometry.vertices->size());
    if (!vertexBuffer) {
      return false;
    }
    auto indexBuffer = GpuBuffer::Make(context, BufferType::Index, geometry.indices->data(),
                                       geometry.indices->size());
    if (!indexBuffer) {
      return false;
    }

    vertexBuffer->assignUniqueKey(atlasProxyKeys[index].first);
    indexBuffer->assignUniqueKey(atlasProxyKeys[index].second);
    index++;
  }

  source = nullptr;
  return true;
}

}  //namespace tgfx