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

#include "MeshBufferUploadTask.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/Log.h"
#include "gpu/resources/BufferResource.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

MeshVertexBufferUploadTask::MeshVertexBufferUploadTask(std::shared_ptr<ResourceProxy> proxy,
                                                       std::shared_ptr<GPUMeshProxy> meshProxy)
    : ResourceTask(std::move(proxy)), meshProxy(std::move(meshProxy)) {
}

std::shared_ptr<Resource> MeshVertexBufferUploadTask::onMakeResource(Context* context) {
  if (meshProxy == nullptr || meshProxy->mesh() == nullptr) {
    return nullptr;
  }

  const auto& impl = MeshImpl::ReadAccess(*meshProxy->mesh());
  size_t vertexDataSize = impl.getVertexStride() * static_cast<size_t>(impl.vertexCount());

  // Allocate temporary buffer and write interleaved vertex data
  auto buffer = std::make_unique<uint8_t[]>(vertexDataSize);
  uint8_t* ptr = buffer.get();
  for (auto i = 0; i < impl.vertexCount(); ++i) {
    // Position (float2)
    memcpy(ptr, &impl.positions()[i], sizeof(Point));
    ptr += sizeof(Point);

    // TexCoord (float2, optional)
    if (impl.hasTexCoords()) {
      memcpy(ptr, &impl.texCoords()[i], sizeof(Point));
      ptr += sizeof(Point);
    }

    // Color (UByte4Normalized, optional)
    if (impl.hasColors()) {
      auto uintColor = ToUintPMColor(impl.colors()[i], nullptr);
      memcpy(ptr, &uintColor, sizeof(uint32_t));
      ptr += sizeof(uint32_t);
    }
  }

  auto gpu = context->gpu();
  auto gpuBuffer = gpu->createBuffer(vertexDataSize, GPUBufferUsage::VERTEX);
  if (!gpuBuffer) {
    LOGE("MeshVertexBufferUploadTask::onMakeResource() Failed to create vertex buffer!");
    return nullptr;
  }

  gpu->queue()->writeBuffer(gpuBuffer, 0, buffer.get(), vertexDataSize);

  return BufferResource::Wrap(context, std::move(gpuBuffer));
}

MeshIndexBufferUploadTask::MeshIndexBufferUploadTask(std::shared_ptr<ResourceProxy> proxy,
                                                     std::shared_ptr<GPUMeshProxy> meshProxy)
    : ResourceTask(std::move(proxy)), meshProxy(std::move(meshProxy)) {
}

std::shared_ptr<Resource> MeshIndexBufferUploadTask::onMakeResource(Context* context) {
  if (meshProxy == nullptr || meshProxy->mesh() == nullptr) {
    return nullptr;
  }

  const auto& impl = MeshImpl::ReadAccess(*meshProxy->mesh());

  if (!impl.hasIndices()) {
    return nullptr;
  }

  size_t indexDataSize = sizeof(uint16_t) * static_cast<size_t>(impl.indexCount());
  auto gpu = context->gpu();
  auto gpuBuffer = gpu->createBuffer(indexDataSize, GPUBufferUsage::INDEX);
  if (!gpuBuffer) {
    LOGE("MeshIndexBufferUploadTask::onMakeResource() Failed to create index buffer!");
    return nullptr;
  }

  gpu->queue()->writeBuffer(gpuBuffer, 0, impl.indices(), indexDataSize);

  return BufferResource::Wrap(context, std::move(gpuBuffer));
}

}  // namespace tgfx
