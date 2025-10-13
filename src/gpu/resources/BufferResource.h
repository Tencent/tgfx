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

#pragma once

#include "gpu/GPU.h"
#include "gpu/GPUBuffer.h"
#include "gpu/resources/Resource.h"

namespace tgfx {
/**
 * BufferResource is a resource that encapsulates a GPUBuffer.
 */
class BufferResource : public Resource {
 public:
  /**
   * Wraps an existing GPUBuffer into a BufferResource.
   */
  static std::shared_ptr<BufferResource> Wrap(Context* context, std::shared_ptr<GPUBuffer> buffer,
                                              const ScratchKey& scratchKey = {}) {
    return Resource::AddToCache(context, new BufferResource(std::move(buffer)), scratchKey);
  }

  size_t memoryUsage() const override {
    return buffer->size();
  }

  /**
   * Returns the size of the BufferResource in bytes.
   */
  size_t size() const {
    return buffer->size();
  }

  /**
   * Returns the GPUBuffer associated with this BufferResource.
   */
  std::shared_ptr<GPUBuffer> gpuBuffer() const {
    return buffer;
  }

 private:
  std::shared_ptr<GPUBuffer> buffer = nullptr;

  explicit BufferResource(std::shared_ptr<GPUBuffer> buffer) : buffer(std::move(buffer)) {
  }
};
}  // namespace tgfx
