/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/utils/Profiling.h"
#include "gpu/GpuBuffer.h"
#include "gpu/Texture.h"

namespace tgfx {
std::shared_ptr<ShapeBufferUploadTask> ShapeBufferUploadTask::MakeFrom(
    UniqueKey trianglesKey, UniqueKey textureKey, std::shared_ptr<ShapeBufferProvider> provider) {
  if (provider == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<ShapeBufferUploadTask>(new ShapeBufferUploadTask(
      std::move(trianglesKey), std::move(textureKey), std::move(provider)));
}

ShapeBufferUploadTask::ShapeBufferUploadTask(UniqueKey trianglesKey, UniqueKey textureKey,
                                             std::shared_ptr<ShapeBufferProvider> provider)
    : ResourceTask(std::move(trianglesKey)), textureKey(std::move(textureKey)),
      provider(std::move(provider)) {
}

bool ShapeBufferUploadTask::execute(Context* context) {
  TRACE_EVENT;
  if (uniqueKey.strongCount() <= 0) {
    // Skip the resource creation if there is no proxy is referencing it.
    return false;
  }
  if (provider == nullptr) {
    return false;
  }
  auto shapeBuffer = provider->getBuffer();
  if (shapeBuffer == nullptr) {
    // No need to log an error here; the shape might not be a filled path or could be invisible.
    return false;
  }
  if (auto triangles = shapeBuffer->triangles()) {
    auto gpuBuffer =
        GpuBuffer::Make(context, BufferType::Vertex, triangles->data(), triangles->size());
    if (!gpuBuffer) {
      LOGE("ShapeBufferUploadTask::execute() Failed to create the GpuBuffer!");
      return false;
    }
    gpuBuffer->assignUniqueKey(uniqueKey);
  } else {
    auto imageBuffer = shapeBuffer->imageBuffer();
    auto texture = Texture::MakeFrom(context, std::move(imageBuffer));
    if (!texture) {
      LOGE("ShapeBufferUploadTask::execute() Failed to create the texture!");
      return false;
    }
    texture->assignUniqueKey(textureKey);
  }
  // Free the data provider immediately to reduce memory pressure.
  provider = nullptr;
  return true;
}
}  // namespace tgfx
