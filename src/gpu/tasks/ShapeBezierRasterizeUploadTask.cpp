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

#include "ShapeBezierRasterizeUploadTask.h"
#include "core/utils/Log.h"
#include "gpu/resources/BufferResource.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
ShapeBezierRasterizeUploadTask::ShapeBezierRasterizeUploadTask(
    std::shared_ptr<ResourceProxy> vertexBufferProxy,
    std::unique_ptr<DataSource<ShapeBezierBuffer>> source)
    : ResourceTask(std::move(vertexBufferProxy)), source(std::move(source)) {
}

std::shared_ptr<Resource> ShapeBezierRasterizeUploadTask::onMakeResource(Context* context) {
  if (source == nullptr) {
    return nullptr;
  }
  auto bezierBuffer = source->getData();
  source = nullptr;
  if (bezierBuffer == nullptr || bezierBuffer->vertices == nullptr ||
      bezierBuffer->vertices->size() == 0) {
    return nullptr;
  }
  auto vertexBuffer =
      BufferResource::FindOrCreate(context, bezierBuffer->vertices->size(), GPUBufferUsage::VERTEX);
  if (vertexBuffer == nullptr) {
    LOGE("ShapeBezierRasterizeUploadTask::onMakeResource() Failed to create vertex buffer!");
    return nullptr;
  }
  context->gpu()->queue()->writeBuffer(vertexBuffer->gpuBuffer(), 0, bezierBuffer->vertices->data(),
                                       bezierBuffer->vertices->size());
  return vertexBuffer;
}
}  // namespace tgfx
