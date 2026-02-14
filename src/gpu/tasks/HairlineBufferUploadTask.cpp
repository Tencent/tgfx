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

  // Create all buffers first, before assigning to proxies.
  // This ensures atomic success/failure behavior.
  bool failed = false;
  auto lineVertexBuffer = createBuffer(context, gpu, hairlineBuffer->lineVertices,
                                        GPUBufferUsage::VERTEX, "line vertex buffer", failed);
  auto lineIndexBuffer = createBuffer(context, gpu, hairlineBuffer->lineIndices,
                                       GPUBufferUsage::INDEX, "line index buffer", failed);
  auto quadVertexBuffer = createBuffer(context, gpu, hairlineBuffer->quadVertices,
                                        GPUBufferUsage::VERTEX, "quad vertex buffer", failed);
  auto quadIndexBuffer = createBuffer(context, gpu, hairlineBuffer->quadIndices,
                                       GPUBufferUsage::INDEX, "quad index buffer", failed);

  if (failed) {
    // Don't assign any buffers to proxies if any creation failed.
    return nullptr;
  }

  // All buffers created successfully (or were not needed). Assign to proxies.
  assignBufferToProxy(lineIndexBuffer, lineIndexProxy);
  assignBufferToProxy(quadVertexBuffer, quadVertexProxy);
  assignBufferToProxy(quadIndexBuffer, quadIndexProxy);

  // Release the source data as it's no longer needed.
  source = nullptr;

  // Return whichever buffer is available. The base class ResourceTask expects a non-null
  // return value when the task succeeds. Prefer lineVertexBuffer, fall back to quadVertexBuffer.
  if (lineVertexBuffer) {
    return lineVertexBuffer;
  }
  return quadVertexBuffer;
}

std::shared_ptr<BufferResource> HairlineBufferUploadTask::createBuffer(
    Context* context, GPU* gpu, const std::shared_ptr<Data>& data, uint32_t usage,
    const char* bufferName, bool& creationFailed) {
  if (!data || data->empty()) {
    return nullptr;
  }

  auto gpuBuffer = gpu->createBuffer(data->size(), usage);
  if (!gpuBuffer) {
    LOGE("HairlineBufferUploadTask: Failed to create %s!", bufferName);
    creationFailed = true;
    return nullptr;
  }

  gpu->queue()->writeBuffer(gpuBuffer, 0, data->data(), data->size());
  return BufferResource::Wrap(context, std::move(gpuBuffer));
}

void HairlineBufferUploadTask::assignBufferToProxy(
    const std::shared_ptr<BufferResource>& buffer,
    const std::shared_ptr<ResourceProxy>& proxy) {
  if (buffer && proxy) {
    proxy->resource = buffer;
  }
}

}  // namespace tgfx
