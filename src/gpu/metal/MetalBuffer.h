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

#pragma once

#include <Metal/Metal.h>
#include "tgfx/gpu/GPUBuffer.h"
#include "MetalResource.h"

namespace tgfx {

class MetalGPU;

/**
 * Metal buffer implementation.
 */
class MetalBuffer : public GPUBuffer, public MetalResource {
 public:
  static std::shared_ptr<MetalBuffer> Make(MetalGPU* gpu, size_t size, uint32_t usage);

  /**
   * Returns the Metal buffer.
   */
  id<MTLBuffer> metalBuffer() const {
    return buffer;
  }

  // GPUBuffer interface implementation
  void* map(size_t offset, size_t size) override;
  void unmap() override;
  bool isReady() const override;

  void insertReadbackFence(id<MTLCommandBuffer> cmdBuffer);

 protected:
  void onRelease(MetalGPU* gpu) override;

 private:
  MetalBuffer(size_t size, uint32_t usage, id<MTLBuffer> metalBuffer);
  ~MetalBuffer() override = default;

  id<MTLBuffer> buffer = nil;
  id<MTLCommandBuffer> pendingCommandBuffer = nil;
  void* mappedPointer = nullptr;
  
  friend class MetalGPU;
};

}  // namespace tgfx