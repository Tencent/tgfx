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

#include "D3D12CommandQueue.h"
#include "D3D12GPU.h"
#include "core/utils/Log.h"

namespace tgfx {

D3D12CommandQueue::D3D12CommandQueue(D3D12GPU* d3d12GPU) : gpu(d3d12GPU) {
  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  auto hr = gpu->device()->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue));
  if (FAILED(hr)) {
    LOGE("D3D12CommandQueue: Failed to create command queue, HRESULT=0x%08X",
         static_cast<unsigned>(hr));
    return;
  }
  hr = gpu->device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
  if (FAILED(hr)) {
    LOGE("D3D12CommandQueue: Failed to create fence, HRESULT=0x%08X", static_cast<unsigned>(hr));
    commandQueue = nullptr;
    return;
  }
  fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (fenceEvent == nullptr) {
    LOGE("D3D12CommandQueue: Failed to create fence event.");
    commandQueue = nullptr;
    fence = nullptr;
  }
}

D3D12CommandQueue::~D3D12CommandQueue() {
  if (fenceEvent != nullptr) {
    CloseHandle(fenceEvent);
    fenceEvent = nullptr;
  }
}

void D3D12CommandQueue::submit(std::shared_ptr<CommandBuffer>) {
  // Will be fully implemented when D3D12CommandBuffer is available (sub-task 6).
  // For now, just signal the fence and process unreferenced resources.
  fenceValue++;
  commandQueue->Signal(fence.Get(), fenceValue);
  gpu->processUnreferencedResources();
}

void D3D12CommandQueue::writeBuffer(std::shared_ptr<GPUBuffer> buffer, size_t bufferOffset,
                                    const void* data, size_t dataSize) {
  if (!buffer || !data || dataSize == 0) {
    return;
  }
  void* mappedData = buffer->map(bufferOffset, dataSize);
  if (mappedData) {
    memcpy(mappedData, data, dataSize);
    buffer->unmap();
  }
}

void D3D12CommandQueue::writeTexture(std::shared_ptr<Texture>, const Rect&, const void*, size_t) {
  // Will be implemented when D3D12Texture is available (sub-task 3).
  // D3D12 textures in default heap always require staging buffer + copy command list.
}

std::shared_ptr<Semaphore> D3D12CommandQueue::insertSemaphore() {
  // Will be implemented when D3D12Semaphore is available (sub-task 6).
  return nullptr;
}

void D3D12CommandQueue::waitSemaphore(std::shared_ptr<Semaphore>) {
  // Will be implemented when D3D12Semaphore is available (sub-task 6).
}

void D3D12CommandQueue::waitForFence(uint64_t targetValue) {
  if (fence == nullptr || fenceEvent == nullptr) {
    return;
  }
  if (fence->GetCompletedValue() < targetValue) {
    fence->SetEventOnCompletion(targetValue, fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);
  }
}

void D3D12CommandQueue::waitUntilCompleted() {
  waitForFence(fenceValue);
}

}  // namespace tgfx
