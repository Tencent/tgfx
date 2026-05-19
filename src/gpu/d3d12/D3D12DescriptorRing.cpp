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

#include "D3D12DescriptorRing.h"
#include "core/utils/Log.h"

namespace tgfx {

bool D3D12DescriptorRing::init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                               uint32_t capacity) {
  if (device == nullptr || capacity == 0) {
    return false;
  }
  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.Type = type;
  desc.NumDescriptors = capacity;
  desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_heap)))) {
    LOGE("D3D12DescriptorRing::init() CreateDescriptorHeap failed (type=%d capacity=%u).",
         static_cast<int>(type), capacity);
    return false;
  }
  _capacity = capacity;
  _descriptorSize = device->GetDescriptorHandleIncrementSize(type);
  cpuBase = _heap->GetCPUDescriptorHandleForHeapStart();
  gpuBase = _heap->GetGPUDescriptorHandleForHeapStart();
  head = 0;
  tail = 0;
  committedHead = 0;
  return true;
}

D3D12DescriptorRing::Range D3D12DescriptorRing::allocate(uint32_t count) {
  if (_heap == nullptr || count == 0 || count > _capacity) {
    return {};
  }
  // Compute available room without crossing tail. Head and tail use a "free slots" model where
  // tail == head means the ring is empty; allocating up to capacity-1 keeps the wrap-around
  // unambiguous.
  uint32_t used = (head >= tail) ? (head - tail) : (_capacity - (tail - head));
  uint32_t free = _capacity - used;
  if (count > free) {
    LOGE("D3D12DescriptorRing::allocate() out of slots: requested=%u free=%u capacity=%u.", count,
         free, _capacity);
    return {};
  }
  // Avoid splitting an allocation across the wrap-around boundary so callers can pass a single
  // contiguous CPU/GPU descriptor range to D3D12 APIs (CreateShaderResourceView,
  // SetGraphicsRootDescriptorTable, etc.). If the tail end of the ring cannot fit the entire
  // request, skip it and start from slot 0.
  uint32_t startSlot = head;
  if (head + count > _capacity) {
    uint32_t skipped = _capacity - head;
    if (skipped > free - count) {
      LOGE(
          "D3D12DescriptorRing::allocate() wrap skip exceeds free space (count=%u free=%u "
          "skipped=%u).",
          count, free, skipped);
      return {};
    }
    startSlot = 0;
  }
  Range range = {};
  range.cpuStart = cpuBase;
  range.cpuStart.ptr += static_cast<SIZE_T>(startSlot) * _descriptorSize;
  range.gpuStart = gpuBase;
  range.gpuStart.ptr += static_cast<UINT64>(startSlot) * _descriptorSize;
  range.startSlot = startSlot;
  range.count = count;
  head = startSlot + count;
  if (head == _capacity) {
    head = 0;
  }
  return range;
}

void D3D12DescriptorRing::commit(uint64_t fenceValue) {
  // No new allocations since last commit — nothing to track.
  if (head == committedHead) {
    return;
  }
  InflightRange entry = {};
  entry.fenceValue = fenceValue;
  entry.newHead = head;
  inflight.push_back(entry);
  committedHead = head;
}

void D3D12DescriptorRing::retire(uint64_t completedFenceValue) {
  while (!inflight.empty() && inflight.front().fenceValue <= completedFenceValue) {
    tail = inflight.front().newHead;
    inflight.pop_front();
  }
}

}  // namespace tgfx
