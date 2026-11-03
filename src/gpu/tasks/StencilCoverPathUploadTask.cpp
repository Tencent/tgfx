/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "StencilCoverPathUploadTask.h"
#include "core/utils/Log.h"
#include "gpu/resources/StencilCoverPathBuffer.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
StencilCoverPathUploadTask::StencilCoverPathUploadTask(
    std::shared_ptr<ResourceProxy> vertexBufferProxy,
    std::unique_ptr<DataSource<StencilCoverVertexData>> source)
    : ResourceTask(std::move(vertexBufferProxy)), source(std::move(source)) {
}

std::shared_ptr<Resource> StencilCoverPathUploadTask::onMakeResource(Context* context) {
  if (source == nullptr) {
    return nullptr;
  }
  auto bezierBuffer = source->getData();
  source = nullptr;
  if (bezierBuffer == nullptr || bezierBuffer->vertices == nullptr ||
      bezierBuffer->vertices->size() == 0) {
    return nullptr;
  }
  auto vertexBuffer = StencilCoverPathBuffer::FindOrCreate(
      context, bezierBuffer->vertices->size(), bezierBuffer->fillType);
  if (vertexBuffer == nullptr) {
    LOGE("StencilCoverPathUploadTask::onMakeResource() Failed to create vertex buffer!");
    return nullptr;
  }
  context->gpu()->queue()->writeBuffer(vertexBuffer->gpuBuffer(), 0, bezierBuffer->vertices->data(),
                                       bezierBuffer->vertices->size());
  return vertexBuffer;
}
}  // namespace tgfx
