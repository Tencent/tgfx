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

#include <memory>
#include <vector>
#include "D3D12GPU.h"
#include "D3D12Util.h"
#include "tgfx/gpu/CommandQueue.h"

namespace tgfx {

class D3D12GPU;
class D3D12Semaphore;

/**
 * Thin coordination layer satisfying the public CommandQueue interface. Mirrors VulkanCommandQueue:
 * holds only the data accumulated between two consecutive submit() calls and delegates submission
 * timing / inflight tracking to D3D12GPU::executeSubmission().
 *
 * Pending state held here:
 *   - pendingUploads: staging UPLOAD buffers from writeTexture(), consumed by submit().
 *   - pendingSignal/WaitSemaphore: from insertSemaphore()/waitSemaphore(), consumed by submit().
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
  void flushUploads(ID3D12GraphicsCommandList* commandList);

  D3D12GPU* gpu = nullptr;
  ComPtr<ID3D12CommandQueue> commandQueue = nullptr;

  // Produced by writeTexture(), consumed by the next submit() (or waitUntilCompleted()) which
  // records CopyTextureRegion commands and then moves the staging buffers into the inflight
  // submission so they can be safely released after the GPU fence signals.
  std::vector<D3D12GPU::PendingUpload> pendingUploads;

  // Per-upload metadata kept alongside pendingUploads so flushUploads can record the GPU copy
  // without re-deriving the row pitch / pixel dimensions.
  struct UploadFootprint {
    // Source ID3D12Resource for CopyTextureRegion. Lifetime is owned either by D3D12GPU's
    // UPLOAD ring (fast path, no extra retention required) or by the matching PendingUpload's
    // stagingBuffer ComPtr (slow / fallback path).
    ID3D12Resource* stagingResource = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT dstX = 0;
    UINT dstY = 0;
    UINT srcWidth = 0;
    UINT srcHeight = 0;
  };
  std::vector<UploadFootprint> pendingFootprints;

  std::shared_ptr<D3D12Semaphore> pendingSignalSemaphore;
  std::shared_ptr<D3D12Semaphore> pendingWaitSemaphore;
};

}  // namespace tgfx
