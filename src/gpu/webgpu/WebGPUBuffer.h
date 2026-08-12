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
#include <memory>
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

  /**
   * Unmaps the buffer, and also cancels an in-flight asynchronous map request. Calling this on a
   * buffer that is neither mapped nor pending is a no-op.
   */
  void unmap() override;

  /**
   * Initiates an asynchronous map request for READBACK buffers. After calling this, poll isReady()
   * to check completion, then call map() to access the data. This method is idempotent: it does
   * nothing while a request is in flight or the buffer is already mapped. If a previous request
   * failed, calling it again issues a new request.
   */
  void requestMapAsync() override;

  /**
   * Marks the buffer as mapped or unmapped. Used by external async mapping paths (e.g., JS-side
   * mapAsync) to notify C++ about the mapping result. Any in-flight request issued from C++ is
   * superseded, so its late callback cannot overwrite the state set here.
   */
  void setMapReady(bool ready);

  void onRelease(WebGPUGPU* gpu) override;

 private:
  enum class MapState { Unmapped, Pending, Mapped };

  /**
   * Shared state for one asynchronous map request. It is kept alive by the map callback, so the
   * callback stays safe even when the WebGPUBuffer has already been destroyed: the owner pointer is
   * cleared before the buffer goes away.
   */
  struct MapRequest {
    WebGPUBuffer* owner = nullptr;
  };

  WebGPUBuffer(WebGPUGPU* gpu, WGPUBuffer buffer, size_t size, uint32_t usage);

  void detachMapRequest();

  static void OnBufferMapped(WGPUBufferMapAsyncStatus status, void* userdata);

  WebGPUGPU* _gpu = nullptr;
  WGPUBuffer buffer = nullptr;
  void* stagingData = nullptr;
  size_t stagingOffset = 0;
  size_t stagingSize = 0;
  MapState mapState = MapState::Unmapped;
  uint32_t mapGeneration = 0;
  std::shared_ptr<MapRequest> mapRequest = nullptr;

  friend class WebGPUGPU;
};

}  // namespace tgfx
