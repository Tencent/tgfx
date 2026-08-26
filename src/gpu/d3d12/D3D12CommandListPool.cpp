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

#include "D3D12CommandListPool.h"
#include "core/utils/Log.h"

namespace tgfx {

D3D12CommandListPool::Entry D3D12CommandListPool::acquire(ID3D12Device* device) {
  Entry result = {};
  if (device == nullptr) {
    return result;
  }
  // Reuse a pooled pair if available. Reset returns the allocator to its initial empty state
  // and rewinds the command list to a fresh recording state — both are cheap (microseconds),
  // unlike CreateCommandAllocator/CreateCommandList which trigger driver-internal allocation.
  while (!freeList.empty()) {
    auto entry = std::move(freeList.back());
    freeList.pop_back();
    if (entry.allocator == nullptr || entry.commandList == nullptr) {
      continue;
    }
    auto hr = entry.allocator->Reset();
    if (FAILED(hr)) {
      LOGE("D3D12CommandListPool::acquire: allocator Reset failed (HRESULT=0x%08X), discarding.",
           static_cast<unsigned>(hr));
      continue;
    }
    hr = entry.commandList->Reset(entry.allocator.Get(), nullptr);
    if (FAILED(hr)) {
      LOGE(
          "D3D12CommandListPool::acquire: command-list Reset failed (HRESULT=0x%08X), "
          "discarding.",
          static_cast<unsigned>(hr));
      continue;
    }
    return entry;
  }
  // Cold path: nothing pooled (or every pooled entry failed to Reset). Build a fresh pair.
  ComPtr<ID3D12CommandAllocator> allocator = nullptr;
  auto hr =
      device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
  if (FAILED(hr)) {
    LOGE("D3D12CommandListPool::acquire: CreateCommandAllocator failed (HRESULT=0x%08X).",
         static_cast<unsigned>(hr));
    return result;
  }
  ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
  hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                 IID_PPV_ARGS(&commandList));
  if (FAILED(hr)) {
    LOGE("D3D12CommandListPool::acquire: CreateCommandList failed (HRESULT=0x%08X).",
         static_cast<unsigned>(hr));
    return result;
  }
  result.allocator = std::move(allocator);
  result.commandList = std::move(commandList);
  return result;
}

void D3D12CommandListPool::release(Entry entry) {
  if (!entry.valid()) {
    return;
  }
  if (freeList.size() >= MAX_POOLED) {
    // Cap the pool so a long-running app with bursty submission patterns doesn't keep an
    // unbounded number of allocators alive. ComPtr destructors release the driver references.
    return;
  }
  freeList.push_back(std::move(entry));
}

void D3D12CommandListPool::clear() {
  freeList.clear();
}

}  // namespace tgfx
