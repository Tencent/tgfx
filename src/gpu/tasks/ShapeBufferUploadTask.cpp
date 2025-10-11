/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "ShapeBufferUploadTask.h"
#include "gpu/GPU.h"
#include "gpu/resources/BufferResource.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {
ShapeBufferUploadTask::ShapeBufferUploadTask(std::shared_ptr<ResourceProxy> trianglesProxy,
                                             std::shared_ptr<ResourceProxy> textureProxy,
                                             std::unique_ptr<DataSource<ShapeBuffer>> source)
    : ResourceTask(std::move(trianglesProxy)), textureProxy(std::move(textureProxy)),
      source(std::move(source)) {
}

std::shared_ptr<Resource> ShapeBufferUploadTask::onMakeResource(Context* context) {
  if (source == nullptr) {
    return nullptr;
  }
  auto shapeBuffer = source->getData();
  if (shapeBuffer == nullptr) {
    // No need to log an error here; the shape might not be a filled path or could be invisible.
    return nullptr;
  }
  std::shared_ptr<BufferResource> vertexBuffer = nullptr;
  if (auto triangles = shapeBuffer->triangles) {
    auto gpu = context->gpu();
    auto gpuBuffer = gpu->createBuffer(triangles->size(), GPUBufferUsage::VERTEX);
    if (!gpuBuffer) {
      LOGE("ShapeBufferUploadTask::onMakeResource() Failed to create buffer!");
      return nullptr;
    }
    if (!gpu->queue()->writeBuffer(gpuBuffer, 0, triangles->data(), triangles->size())) {
      LOGE("ShapeBufferUploadTask::onMakeResource() Failed to write buffer!");
      return nullptr;
    }
    vertexBuffer = Resource::AddToCache(context, new BufferResource(std::move(gpuBuffer)));
  } else {
    auto textureView = TextureView::MakeFrom(context, std::move(shapeBuffer->imageBuffer));
    if (!textureView) {
      LOGE("ShapeBufferUploadTask::execute() Failed to create the texture view!");
      return nullptr;
    }
    textureView->assignUniqueKey(textureProxy->uniqueKey);
    textureProxy->resource = std::move(textureView);
  }
  // Free the data source immediately to reduce memory pressure.
  source = nullptr;
  return vertexBuffer;
}
}  // namespace tgfx
