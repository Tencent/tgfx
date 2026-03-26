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

#include "D3D12Util.h"
#include "tgfx/gpu/CommandQueue.h"

namespace tgfx {

class D3D12GPU;
class D3D12Semaphore;

/**
 * D3D12 command queue implementation.
 */
class D3D12CommandQueue : public CommandQueue {
 public:
  explicit D3D12CommandQueue(D3D12GPU* gpu);
  ~D3D12CommandQueue() override;

  ID3D12CommandQueue* d3d12CommandQueue() const {
    return commandQueue.Get();
  }

  void submit(std::shared_ptr<CommandBuffer> commandBuffer) override;

  void writeBuffer(std::shared_ptr<GPUBuffer> buffer, size_t bufferOffset, const void* data,
                   size_t dataSize) override;

  void writeTexture(std::shared_ptr<Texture> texture, const Rect& rect, const void* pixels,
                    size_t rowBytes) override;

  std::shared_ptr<Semaphore> insertSemaphore() override;

  void waitSemaphore(std::shared_ptr<Semaphore> semaphore) override;

  void waitUntilCompleted() override;

 private:
  void waitForFence(uint64_t fenceValue);

  D3D12GPU* gpu = nullptr;
  ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
  ComPtr<ID3D12Fence> fence = nullptr;
  uint64_t fenceValue = 0;
  HANDLE fenceEvent = nullptr;
  std::shared_ptr<D3D12Semaphore> pendingSignalSemaphore = nullptr;
  std::shared_ptr<D3D12Semaphore> pendingWaitSemaphore = nullptr;
};

}  // namespace tgfx
