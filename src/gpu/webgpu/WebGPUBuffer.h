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

#include <webgpu/webgpu.h>
#include "WebGPUResource.h"
#include "tgfx/gpu/GPUBuffer.h"

namespace tgfx {

class WebGPUGPU;

class WebGPUBuffer : public GPUBuffer, public WebGPUResource {
 public:
  static std::shared_ptr<WebGPUBuffer> Make(WebGPUGPU* gpu, size_t size, uint32_t usage);

  WGPUBuffer webgpuBuffer() const {
    return buffer;
  }

  bool isReady() const override;

  void* map(size_t offset = 0, size_t size = GPU_BUFFER_WHOLE_SIZE) override;

  void unmap() override;

  /**
   * Initiates an asynchronous map request for READBACK buffers. After calling this, poll isReady()
   * to check completion, then call map() to access the data.
   */
  void requestMapAsync() override;

  /**
   * Marks the buffer as mapped (ready for map() call). Used by external async mapping paths
   * (e.g., JS-side mapAsync) to notify C++ that the buffer is now accessible.
   */
  void setMapReady(bool ready) {
    mapReady = ready;
  }

  void onRelease(WebGPUGPU* gpu) override;

 private:
  WebGPUBuffer(WebGPUGPU* gpu, WGPUBuffer buffer, size_t size, uint32_t usage);

  WebGPUGPU* _gpu = nullptr;
  WGPUBuffer buffer = nullptr;
  void* stagingData = nullptr;
  size_t stagingOffset = 0;
  size_t stagingSize = 0;
  bool mapReady = false;

  friend class WebGPUGPU;
};

}  // namespace tgfx
