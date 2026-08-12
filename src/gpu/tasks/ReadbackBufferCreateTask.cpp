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
#include "gpu/resources/BufferResource.h"

namespace tgfx {
ReadbackBufferCreateTask::ReadbackBufferCreateTask(std::shared_ptr<GPUBufferProxy> proxy,
                                                   size_t size)
    : ResourceTask(std::move(proxy)), size(size) {
  DEBUG_ASSERT(size > 0);
}

std::shared_ptr<Resource> ReadbackBufferCreateTask::onMakeResource(Context* context) {
  auto bufferResource = BufferResource::FindOrCreate(context, size, GPUBufferUsage::READBACK);
  if (!bufferResource) {
    LOGE("ReadbackBufferCreateTask::onMakeResource() Failed to create buffer!");
    return nullptr;
  }
  // A recycled readback buffer may still carry the map state of a previous readback that was
  // abandoned before its pixels were consumed. Without this reset, backends with asynchronous
  // mapping would encode a copy into a still-mapped buffer, and a late map callback could make the
  // buffer report ready while holding the previous readback's pixels. This is the only place where
  // a pooled readback buffer is handed out, so resetting here covers every reuse path. Backends
  // without asynchronous mapping treat unmap() on an unmapped buffer as a no-op.
  bufferResource->gpuBuffer()->unmap();
  return bufferResource;
}

}  // namespace tgfx
