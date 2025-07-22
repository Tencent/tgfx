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
#include "gpu/GPUBuffer.h"
#include "gpu/Texture.h"

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
  std::shared_ptr<GPUBuffer> gpuBuffer = nullptr;
  if (auto triangles = shapeBuffer->triangles) {
    gpuBuffer = GPUBuffer::Make(context, BufferType::Vertex, triangles->data(), triangles->size());
    if (!gpuBuffer) {
      LOGE("ShapeBufferUploadTask::execute() Failed to create the GPUBuffer!");
      return nullptr;
    }
  } else {
    auto texture = Texture::MakeFrom(context, std::move(shapeBuffer->imageBuffer));
    if (!texture) {
      LOGE("ShapeBufferUploadTask::execute() Failed to create the texture!");
      return nullptr;
    }
    texture->assignUniqueKey(textureKey);
    textureProxy->resource = std::move(texture);
  }
  // Free the data source immediately to reduce memory pressure.
  source = nullptr;
  return gpuBuffer;
}
}  // namespace tgfx
