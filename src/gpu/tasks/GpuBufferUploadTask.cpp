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

#include "GpuBufferUploadTask.h"
#include "tgfx/core/Task.h"

namespace tgfx {
GpuBufferUploadTask::GpuBufferUploadTask(UniqueKey uniqueKey, BufferType bufferType,
                                         std::unique_ptr<DataSource<Data>> source)
    : ResourceTask(std::move(uniqueKey)), bufferType(bufferType), source(std::move(source)) {
}

std::shared_ptr<Resource> GpuBufferUploadTask::onMakeResource(Context* context) {
  if (source == nullptr) {
    return nullptr;
  }
  auto data = source->getData();
  if (data == nullptr || data->empty()) {
    LOGE("GpuBufferUploadTask::onMakeResource() Failed to get data!");
    return nullptr;
  }
  auto gpuBuffer = GpuBuffer::Make(context, bufferType, data->data(), data->size());
  if (gpuBuffer == nullptr) {
    LOGE("GpuBufferUploadTask::onMakeResource failed to upload the GpuBuffer!");
  } else {
    // Free the data source immediately to reduce memory pressure.
    source = nullptr;
  }
  return gpuBuffer;
}
}  // namespace tgfx
