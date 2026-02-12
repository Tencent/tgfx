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
#include "MtlResource.h"

namespace tgfx {

class MtlGPU;

/**
 * Metal buffer implementation.
 */
class MtlBuffer : public GPUBuffer, public MtlResource {
 public:
  static std::shared_ptr<MtlBuffer> Make(MtlGPU* gpu, size_t size, uint32_t usage);

  /**
   * Returns the Metal buffer.
   */
  id<MTLBuffer> mtlBuffer() const {
    return buffer;
  }

  // GPUBuffer interface implementation
  void* map(size_t offset, size_t size) override;
  void unmap() override;
  bool isReady() const override;

  void insertReadbackFence(id<MTLCommandBuffer> cmdBuffer);

 protected:
  void onRelease(MtlGPU* gpu) override;

 private:
  MtlBuffer(size_t size, uint32_t usage, id<MTLBuffer> mtlBuffer);
  ~MtlBuffer() override = default;

  id<MTLBuffer> buffer = nil;
  id<MTLCommandBuffer> pendingCommandBuffer = nil;
  void* mappedPointer = nullptr;
  
  friend class MtlGPU;
};

}  // namespace tgfx