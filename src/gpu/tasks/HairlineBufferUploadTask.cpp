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
    std::shared_ptr<ResourceProxy> quadVertexProxy,
    std::unique_ptr<DataSource<HairlineBuffer>> source)
    : ResourceTask(std::move(lineVertexProxy)), quadVertexProxy(std::move(quadVertexProxy)),
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

  // Create all buffers first, before assigning to proxies.
  // This ensures atomic success/failure behavior.
  bool failed = false;
  auto lineVertexBuffer = createBuffer(context, gpu, hairlineBuffer->lineVertices,
                                        GPUBufferUsage::VERTEX, "line vertex buffer", &failed);
  auto quadVertexBuffer = createBuffer(context, gpu, hairlineBuffer->quadVertices,
                                        GPUBufferUsage::VERTEX, "quad vertex buffer", &failed);

  if (failed) {
    // Don't assign any buffers to proxies if any creation failed.
    return nullptr;
  }

  // All buffers created successfully (or were not needed).
  // quadVertexProxy is managed here; lineVertexProxy is managed by the base class.
  if (quadVertexBuffer && quadVertexProxy) {
    if (!quadVertexProxy->uniqueKey.empty()) {
      quadVertexBuffer->assignUniqueKey(quadVertexProxy->uniqueKey);
    }
    quadVertexProxy->resource = quadVertexBuffer;
  }

  // Release the source data as it's no longer needed.
  source = nullptr;

  // Return lineVertexBuffer for the base class to assign to lineVertexProxy.
  // If lineVertexBuffer is nullptr (only quad data exists), the base class returns early,
  // but quadVertexProxy is already correctly set above.
  return lineVertexBuffer;
}

std::shared_ptr<BufferResource> HairlineBufferUploadTask::createBuffer(
    Context* context, GPU* gpu, const std::shared_ptr<Data>& data, uint32_t usage,
    const char* bufferName, bool* creationFailed) {
  if (!data || data->empty()) {
    return nullptr;
  }

  auto gpuBuffer = gpu->createBuffer(data->size(), usage);
  if (!gpuBuffer) {
    LOGE("HairlineBufferUploadTask: Failed to create %s!", bufferName);
    *creationFailed = true;
    return nullptr;
  }

  gpu->queue()->writeBuffer(gpuBuffer, 0, data->data(), data->size());
  return BufferResource::Wrap(context, std::move(gpuBuffer));
}

}  // namespace tgfx
