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

#include <deque>
#include "D3D12Util.h"

namespace tgfx {

/**
 * Single shader-visible descriptor heap used as a fence-tracked ring buffer.
 *
 * Rationale:
 *   - D3D12 shader-visible descriptor heaps are expensive to create (they reserve a GPU virtual
 *     address range and the runtime caps total live shader-visible descriptors per heap type).
 *   - The naive "create one heap per render pass" pattern hits driver limits and burns CPU on
 *     every submission. The standard D3D12 idiom is one large heap per heap type, sub-allocated
 *     linearly with fence-based reclamation.
 *
 * Allocation model:
 *   - allocate(count) hands out a contiguous slot range from a monotonic head pointer.
 *   - commit(fenceValue) snapshots head: every slot allocated since the last commit is now
 *     "owned" by `fenceValue` and will be reclaimed once the GPU signals it.
 *   - retire(completedFenceValue) advances tail past every committed range whose fence has
 *     completed, freeing those slots for re-allocation.
 *   - When head wraps around back near tail, allocate() returns an invalid Range; the caller
 *     must treat that as a hard failure (capacity should be sized to make this unreachable in
 *     normal use).
 *
 * Thread safety: not thread-safe. Caller must serialise all access. Matches tgfx's overall
 * single-threaded D3D12 backend usage.
 */
class D3D12DescriptorRing {
 public:
  struct Range {
    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {};
    uint32_t startSlot = 0;
    uint32_t count = 0;
    bool valid() const {
      return count > 0;
    }
  };

  D3D12DescriptorRing() = default;

  /**
   * Creates the underlying shader-visible heap. Pass D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV or
   * D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER. Returns false if the device call fails.
   */
  bool init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity);

  /**
   * Sub-allocates `count` consecutive slots. Returns an invalid Range if the ring cannot satisfy
   * the request without overrunning still-in-flight slots.
   */
  Range allocate(uint32_t count);

  /**
   * Marks every slot allocated since the previous commit() as belonging to `fenceValue`. Those
   * slots become reclaimable only after the GPU advances the fence past `fenceValue`.
   */
  void commit(uint64_t fenceValue);

  /**
   * Reclaims slots whose owning fence value is at or below `completedFenceValue`. Cheap;
   * intended to be called from the same place as the existing inflight-submission polling.
   */
  void retire(uint64_t completedFenceValue);

  ID3D12DescriptorHeap* heap() const {
    return _heap.Get();
  }

  uint32_t descriptorSize() const {
    return _descriptorSize;
  }

  uint32_t capacity() const {
    return _capacity;
  }

 private:
  // Head/tail are slot indices modulo capacity. The (head, tail) pair alone cannot disambiguate
  // "ring empty" from "ring full" — both produce head == tail — so we maintain an explicit
  // outstandingSlots counter that allocate() bumps and retire() drains. Without this an
  // allocation that spans the entire capacity (or a sequence whose head wraps right onto tail)
  // would convince the next allocate() that the ring is empty and hand back slots the GPU is
  // still reading.
  uint32_t head = 0;
  uint32_t tail = 0;
  // Snapshot of head at the last commit() call. Slots in [committedHead, head) are part of the
  // current pending submission; slots before that have already been associated with a fence.
  uint32_t committedHead = 0;
  // Slots currently held by either an as-yet-uncommitted allocation or an inflight commit
  // waiting on its fence. allocate() rejects the request when needed > capacity - outstanding.
  uint32_t outstandingSlots = 0;

  struct InflightRange {
    uint64_t fenceValue = 0;
    // Snapshot of head taken at commit() time. When fenceValue completes, tail can advance up
    // to this point.
    uint32_t newHead = 0;
    // Slots consumed between the previous commit() and this one (including any wrap-around
    // skip), returned to outstandingSlots when retire() reaches this entry.
    uint32_t slots = 0;
  };
  std::deque<InflightRange> inflight;

  ComPtr<ID3D12DescriptorHeap> _heap = nullptr;
  uint32_t _capacity = 0;
  uint32_t _descriptorSize = 0;
  D3D12_CPU_DESCRIPTOR_HANDLE cpuBase = {};
  D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = {};
};

}  // namespace tgfx
