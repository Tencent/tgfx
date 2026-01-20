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

#include <webgpu/webgpu_cpp.h>
#include "tgfx/gpu/GPUBuffer.h"

namespace tgfx {

class WebGPUBuffer : public GPUBuffer {
 public:
  WebGPUBuffer(wgpu::Buffer buffer, wgpu::Queue queue, size_t size, uint32_t usage);

  ~WebGPUBuffer() override;

  wgpu::Buffer wgpuBuffer() const {
    return _buffer;
  }

  bool isReady() const override;

  void* map(size_t offset, size_t size) override;

  void unmap() override;

 private:
  wgpu::Buffer _buffer = nullptr;
  wgpu::Queue _queue = nullptr;
  size_t mappedOffset = 0;
  size_t mappedSize = 0;
  void* localBuffer = nullptr;
};
}  // namespace tgfx
