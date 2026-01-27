/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "HairlineBufferUploadTask.h"
#include "gpu/resources/BufferResource.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

HairlineBufferUploadTask::HairlineBufferUploadTask(
    std::shared_ptr<ResourceProxy> lineVertexProxy,
    std::shared_ptr<ResourceProxy> lineIndexProxy,
    std::shared_ptr<ResourceProxy> quadVertexProxy,
    std::shared_ptr<ResourceProxy> quadIndexProxy,
    std::unique_ptr<DataSource<HairlineBuffer>> source)
    : ResourceTask(std::move(lineVertexProxy)), lineIndexProxy(std::move(lineIndexProxy)),
      quadVertexProxy(std::move(quadVertexProxy)), quadIndexProxy(std::move(quadIndexProxy)),
      source(std::move(source)) {
}

std::shared_ptr<Resource> HairlineBufferUploadTask::onMakeResource(Context* context) {
  if (source == nullptr) {
    return nullptr;
  }

  auto hairlineBuffer = source->getData();
  if (hairlineBuffer == nullptr) {
    return nullptr;
  }

  auto gpu = context->gpu();
  std::shared_ptr<BufferResource> lineVertexBuffer = nullptr;

  // Create line vertex buffer
  if (hairlineBuffer->lineVertices && !hairlineBuffer->lineVertices->empty()) {
    auto gpuBuffer =
        gpu->createBuffer(hairlineBuffer->lineVertices->size(), GPUBufferUsage::VERTEX);
    if (!gpuBuffer) {
      LOGE("HairlineBufferUploadTask: Failed to create line vertex buffer!");
      return nullptr;
    }
    gpu->queue()->writeBuffer(gpuBuffer, 0, hairlineBuffer->lineVertices->data(),
                              hairlineBuffer->lineVertices->size());
    lineVertexBuffer = BufferResource::Wrap(context, std::move(gpuBuffer));
  }

  // Create line index buffer
  if (hairlineBuffer->lineIndices && !hairlineBuffer->lineIndices->empty()) {
    auto gpuBuffer =
        gpu->createBuffer(hairlineBuffer->lineIndices->size(), GPUBufferUsage::INDEX);
    if (!gpuBuffer) {
      LOGE("HairlineBufferUploadTask: Failed to create line index buffer!");
      return nullptr;
    }
    gpu->queue()->writeBuffer(gpuBuffer, 0, hairlineBuffer->lineIndices->data(),
                              hairlineBuffer->lineIndices->size());
    auto lineIndexBuffer = BufferResource::Wrap(context, std::move(gpuBuffer));
    if (lineIndexProxy) {
      lineIndexBuffer->assignUniqueKey(lineIndexProxy->uniqueKey);
      lineIndexProxy->resource = lineIndexBuffer;
    }
  }

  // Create quad vertex buffer
  if (hairlineBuffer->quadVertices && !hairlineBuffer->quadVertices->empty()) {
    auto gpuBuffer =
        gpu->createBuffer(hairlineBuffer->quadVertices->size(), GPUBufferUsage::VERTEX);
    if (!gpuBuffer) {
      LOGE("HairlineBufferUploadTask: Failed to create quad vertex buffer!");
      return nullptr;
    }
    gpu->queue()->writeBuffer(gpuBuffer, 0, hairlineBuffer->quadVertices->data(),
                              hairlineBuffer->quadVertices->size());
    auto quadVertexBuffer = BufferResource::Wrap(context, std::move(gpuBuffer));
    if (quadVertexProxy) {
      quadVertexBuffer->assignUniqueKey(quadVertexProxy->uniqueKey);
      quadVertexProxy->resource = quadVertexBuffer;
    }
  }

  // Create quad index buffer
  if (hairlineBuffer->quadIndices && !hairlineBuffer->quadIndices->empty()) {
    auto gpuBuffer =
        gpu->createBuffer(hairlineBuffer->quadIndices->size(), GPUBufferUsage::INDEX);
    if (!gpuBuffer) {
      LOGE("HairlineBufferUploadTask: Failed to create quad index buffer!");
      return nullptr;
    }
    gpu->queue()->writeBuffer(gpuBuffer, 0, hairlineBuffer->quadIndices->data(),
                              hairlineBuffer->quadIndices->size());
    auto quadIndexBuffer = BufferResource::Wrap(context, std::move(gpuBuffer));
    if (quadIndexProxy) {
      quadIndexBuffer->assignUniqueKey(quadIndexProxy->uniqueKey);
      quadIndexProxy->resource = quadIndexBuffer;
    }
  }

  source = nullptr;
  return lineVertexBuffer;
}

}  // namespace tgfx
