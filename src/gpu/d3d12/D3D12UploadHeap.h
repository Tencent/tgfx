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
 * Single permanently-mapped UPLOAD heap used as a byte-level fence-tracked ring for staging
 * data on its way to GPU resources (texture pixel uploads, buffer writes, etc).
 *
 * Why not per-call CreateCommittedResource:
 *   - Every D3D12 CreateCommittedResource(UPLOAD) call goes through the kernel allocator and
 *     reserves a fresh GPU virtual address range. For high-frequency uploads (writeTexture()
 *     called once per glyph atlas update, per render pass blur seed, etc) this accumulates real
 *     CPU and driver memory cost.
 *   - The standard solution is a single big UPLOAD buffer that stays Mapped() for its entire
 *     lifetime; sub-allocations are pure pointer arithmetic.
 *
 * Allocation model:
 *   - allocate(size, alignment) returns an Allocation that names a contiguous byte range inside
 *     the underlying ID3D12Resource. The caller writes pixel data via the cpu pointer and uses
 *     resource() + offsetInResource as the source of CopyTextureRegion / CopyBufferRegion.
 *   - commit(fenceValue) snapshots head: every byte allocated since the last commit is now
 *     "owned" by `fenceValue` and must outlive its signal.
 *   - retire(completedFenceValue) reclaims byte ranges whose fence has signalled.
 *
 * Capacity management:
 *   - A starting capacity is allocated up front. If a single allocation exceeds capacity OR the
 *     ring is fully in flight, allocate() returns an invalid Allocation; the caller must fall
 *     back to a one-off CreateCommittedResource. This avoids the complications of mid-frame
 *     resource recreation while still letting the steady-state path skip the slow allocator.
 *
 * Thread safety: not thread-safe. Caller serialises access (matches the rest of the D3D12
 * backend).
 */
class D3D12UploadHeap {
 public:
  struct Allocation {
    // Lifetime-stable raw pointer; the heap's underlying ID3D12Resource is kept alive by the
    // D3D12UploadHeap instance (which lives on D3D12GPU). Callers must not extend its lifetime
    // beyond the next D3D12GPU shutdown.
    ID3D12Resource* resource = nullptr;
    void* cpu = nullptr;
    uint64_t gpuVirtualAddress = 0;
    uint64_t offsetInResource = 0;
    size_t size = 0;
    bool valid() const {
      return resource != nullptr && cpu != nullptr && size > 0;
    }
  };

  D3D12UploadHeap() = default;

  /**
   * Creates the underlying UPLOAD-heap committed resource and Map()s it permanently. Returns
   * false on failure (logged inside).
   */
  bool init(ID3D12Device* device, size_t capacity);

  /**
   * Sub-allocates `size` bytes aligned to `alignment` within the ring. Returns an invalid
   * Allocation if the ring cannot satisfy the request without overrunning still-in-flight
   * bytes; callers must then fall back to a per-call upload buffer.
   */
  Allocation allocate(size_t size, size_t alignment);

  /**
   * Tags every allocation since the last commit() with `fenceValue`. The bytes become
   * reclaimable in retire() once the GPU advances past `fenceValue`.
   */
  void commit(uint64_t fenceValue);

  /**
   * Reclaims byte ranges whose fence has signalled. Cheap to call.
   */
  void retire(uint64_t completedFenceValue);

  /**
   * Drops the underlying resource. Used by D3D12GPU::releaseAll on shutdown. After this call
   * allocate() returns invalid until init() is invoked again.
   */
  void clear();

  /**
   * Drops every inflight byte range and zeroes the head / outstanding bookkeeping while
   * keeping the mapped UPLOAD ID3D12Resource alive. Used by the context-lost recovery path so
   * the ring stops accumulating inflight bytes whose fences will never advance, which would
   * otherwise saturate outstandingBytes and reject every future allocation even though the
   * GPU is no longer touching anything.
   */
  void resetForContextLost();

  size_t capacity() const {
    return _capacity;
  }

 private:
  ComPtr<ID3D12Resource> _resource = nullptr;
  void* mappedCpu = nullptr;
  uint64_t gpuVA = 0;
  size_t _capacity = 0;
  size_t head = 0;
  size_t committedHead = 0;
  // Bytes currently held by either a still-uncommitted allocation or an inflight commit waiting
  // for its fence to signal. Tracked explicitly so allocate() can know how many bytes are still
  // in use — head alone cannot distinguish "ring empty" from "ring full" when an allocation
  // wraps head right back to where it started.
  size_t outstandingBytes = 0;

  struct InflightRange {
    uint64_t fenceValue = 0;
    // Bytes consumed between the previous commit() and this one; returned to outstandingBytes
    // when retire() reaches this entry.
    size_t bytes = 0;
  };
  std::deque<InflightRange> inflight;
};

}  // namespace tgfx
