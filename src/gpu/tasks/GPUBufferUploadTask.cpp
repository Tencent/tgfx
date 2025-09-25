/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "GPUBufferUploadTask.h"
#include "gpu/GPU.h"
#include "gpu/resources/IndexBuffer.h"
#include "gpu/resources/VertexBuffer.h"
#include "inspect/InspectorMark.h"

namespace tgfx {
GPUBufferUploadTask::GPUBufferUploadTask(std::shared_ptr<ResourceProxy> proxy,
                                         BufferType bufferType,
                                         std::unique_ptr<DataSource<Data>> source)
    : ResourceTask(std::move(proxy)), bufferType(bufferType), source(std::move(source)) {
}

std::shared_ptr<Resource> GPUBufferUploadTask::onMakeResource(Context* context) {
  TASK_MARK(tgfx::inspect::OpTaskType::GpuUploadTask);
  ATTRIBUTE_ENUM(bufferType, tgfx::inspect::CustomEnumType::BufferType);
  if (source == nullptr) {
    return nullptr;
  }
  auto data = source->getData();
  if (data == nullptr || data->empty()) {
    LOGE("GPUBufferUploadTask::onMakeResource() Failed to get data!");
    return nullptr;
  }
  auto gpu = context->gpu();
  auto usage = bufferType == BufferType::Index ? GPUBufferUsage::INDEX : GPUBufferUsage::VERTEX;
  auto gpuBuffer = gpu->createBuffer(data->size(), usage);
  if (!gpuBuffer) {
    LOGE("GPUBufferUploadTask::onMakeResource() Failed to create buffer!");
    return nullptr;
  }
  if (!gpu->queue()->writeBuffer(gpuBuffer, 0, data->data(), data->size())) {
    LOGE("GPUBufferUploadTask::onMakeResource() Failed to write buffer!");
    return nullptr;
  }
  // Free the data source immediately to reduce memory pressure.
  source = nullptr;
  if (bufferType == BufferType::Index) {
    return Resource::AddToCache(context, new IndexBuffer(std::move(gpuBuffer)));
  }
  return Resource::AddToCache(context, new VertexBuffer(std::move(gpuBuffer)));
}
}  // namespace tgfx
