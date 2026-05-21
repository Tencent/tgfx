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
                               uint32_t capacity, bool shaderVisible) {
  if (device == nullptr || capacity == 0) {
    return false;
  }
  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.Type = type;
  desc.NumDescriptors = capacity;
  // SHADER_VISIBLE is illegal on RTV/DSV heaps; callers that build those rings pass
  // shaderVisible=false and skip the gpuBase initialisation below so the ring still works for
  // CPU-side allocation but never hands out a GPU handle.
  desc.Flags =
      shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_heap)))) {
    LOGE("D3D12DescriptorRing::init() CreateDescriptorHeap failed (type=%d capacity=%u).",
         static_cast<int>(type), capacity);
    return false;
  }
  _capacity = capacity;
  _descriptorSize = device->GetDescriptorHandleIncrementSize(type);
  cpuBase = _heap->GetCPUDescriptorHandleForHeapStart();
  gpuBase =
      shaderVisible ? _heap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
  head = 0;
  tail = 0;
  committedHead = 0;
  outstandingSlots = 0;
  return true;
}

D3D12DescriptorRing::Range D3D12DescriptorRing::allocate(uint32_t count) {
  if (_heap == nullptr || count == 0 || count > _capacity) {
    return {};
  }
  // Use the explicit outstandingSlots counter rather than head/tail arithmetic. Plain
  // (head, tail) bookkeeping cannot tell "empty" from "full" once an allocation pushes head
  // back onto tail — see the rationale on outstandingSlots in the header.
  uint32_t free = _capacity - outstandingSlots;
  uint32_t needed = count;
  uint32_t startSlot = head;
  uint32_t skipped = 0;
  if (head + count > _capacity) {
    // Avoid splitting an allocation across the wrap-around boundary so callers can pass a
    // single contiguous CPU/GPU descriptor range to D3D12 APIs (CreateShaderResourceView,
    // SetGraphicsRootDescriptorTable, etc.). The discarded tail slots are billed against the
    // same free pool so the ring stays accounting-consistent.
    skipped = _capacity - head;
    needed = count + skipped;
    startSlot = 0;
  }
  if (needed > free) {
    LOGE(
        "D3D12DescriptorRing::allocate() out of slots: requested=%u free=%u capacity=%u "
        "skipped=%u.",
        count, free, _capacity, skipped);
    return {};
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
  outstandingSlots += needed;
  return range;
}

void D3D12DescriptorRing::commit(uint64_t fenceValue) {
  // Compute slots consumed since the previous commit including any wrap-around skip. Comparing
  // (head, committedHead) directly fails the "first allocation took the entire capacity" case
  // because head wraps right back to committedHead — guard that with outstandingSlots.
  uint32_t bytesSinceCommit =
      (head >= committedHead) ? (head - committedHead) : (_capacity - (committedHead - head));
  if (bytesSinceCommit == 0) {
    if (outstandingSlots == 0) {
      // Truly nothing happened since the last commit; skip enqueuing an empty fence record.
      return;
    }
    // The ring was filled to exactly capacity since the last commit, so head == committedHead
    // again. Charge the entire capacity to this fence so retire() returns it eventually.
    bytesSinceCommit = _capacity;
  }
  InflightRange entry = {};
  entry.fenceValue = fenceValue;
  entry.newHead = head;
  entry.slots = bytesSinceCommit;
  inflight.push_back(entry);
  committedHead = head;
}

void D3D12DescriptorRing::retire(uint64_t completedFenceValue) {
  while (!inflight.empty() && inflight.front().fenceValue <= completedFenceValue) {
    tail = inflight.front().newHead;
    if (outstandingSlots >= inflight.front().slots) {
      outstandingSlots -= inflight.front().slots;
    } else {
      // Defensive: bookkeeping should never drop below zero. If it does we reset rather than
      // wrap to UINT32_MAX and stop accepting allocations forever.
      outstandingSlots = 0;
    }
    inflight.pop_front();
  }
}

void D3D12DescriptorRing::resetForContextLost() {
  inflight.clear();
  head = 0;
  tail = 0;
  committedHead = 0;
  outstandingSlots = 0;
}

}  // namespace tgfx
