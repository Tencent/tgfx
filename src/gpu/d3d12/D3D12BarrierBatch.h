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

#include <vector>
#include "D3D12Util.h"

namespace tgfx {

/**
 * Accumulates D3D12 resource transitions and flushes them in a single ResourceBarrier call.
 *
 * Why batching matters:
 *   - Each ID3D12GraphicsCommandList::ResourceBarrier call has fixed CPU-side runtime/driver
 *     cost (state-machine bookkeeping, debug-layer validation, and on some GPUs an implicit
 *     pipeline stall to flush caches).
 *   - When N independent barriers are submitted in a single call, the driver can collapse
 *     redundant cache flushes — for example five textures all moving from RENDER_TARGET to
 *     PIXEL_SHADER_RESOURCE only require one RT-cache flush + one PS-cache invalidate, not
 *     five of each. NVIDIA / AMD / Intel guidance lists barrier batching as one of the top
 *     three D3D12 performance pitfalls.
 *
 * Usage pattern:
 *   D3D12BarrierBatch batch;
 *   batch.addTransition(rt, COMMON, RENDER_TARGET);
 *   batch.addTransition(dsv, COMMON, DEPTH_WRITE);
 *   batch.flush(commandList);   // one ResourceBarrier(2, ...)
 *
 * Thread safety: not thread-safe. Each batch is local to one command-list recording context.
 */
class D3D12BarrierBatch {
 public:
  D3D12BarrierBatch() {
    // Most batches in the backend hold 1–8 transitions (e.g. one per color attachment, or one
    // per sampled texture in a draw). Pre-reserve to avoid the small-buffer reallocation
    // sequence that plain push_back() incurs.
    barriers.reserve(8);
  }

  D3D12BarrierBatch(const D3D12BarrierBatch&) = delete;
  D3D12BarrierBatch& operator=(const D3D12BarrierBatch&) = delete;

  /**
   * Queues a transition barrier. No-op if `before == after` so callers can pass through
   * already-correct states without polluting the batch. `subresource` defaults to "all
   * subresources" which is what most call sites need; callers manipulating individual mips
   * should pass the explicit subresource index.
   */
  void addTransition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                     D3D12_RESOURCE_STATES after,
                     UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

  /**
   * Submits all queued barriers to the command list as a single ResourceBarrier(N, ...) call
   * and clears the batch. Safe to call when the batch is empty (no-op).
   */
  void flush(ID3D12GraphicsCommandList* commandList);

  bool empty() const {
    return barriers.empty();
  }

  size_t size() const {
    return barriers.size();
  }

 private:
  std::vector<D3D12_RESOURCE_BARRIER> barriers;
};

}  // namespace tgfx
