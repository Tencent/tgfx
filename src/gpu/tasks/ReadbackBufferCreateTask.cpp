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

#include "ReadbackBufferCreateTask.h"
#include "core/utils/UniqueID.h"
#include "gpu/resources/BufferResource.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
ReadbackBufferCreateTask::ReadbackBufferCreateTask(std::shared_ptr<GPUBufferProxy> proxy,
                                                   size_t size)
    : ResourceTask(std::move(proxy)), size(size) {
  DEBUG_ASSERT(size > 0);
}

std::shared_ptr<Resource> ReadbackBufferCreateTask::onMakeResource(Context* context) {
  static const uint32_t ReadbackBufferType = UniqueID::Next();
  BytesKey bytesKey(2);
  bytesKey.write(ReadbackBufferType);
  bytesKey.write(static_cast<uint32_t>(size));
  ScratchKey scratchKey = bytesKey;
  auto resource = Resource::Find<BufferResource>(context, scratchKey);
  if (resource != nullptr) {
    return resource;
  }
  auto gpu = context->gpu();
  auto gpuBuffer = gpu->createBuffer(size, GPUBufferUsage::READBACK);
  if (!gpuBuffer) {
    LOGE("ReadbackBufferCreateTask::onMakeResource() Failed to create buffer!");
    return nullptr;
  }
  return BufferResource::Wrap(context, std::move(gpuBuffer), scratchKey);
}

}  // namespace tgfx
