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
 * Pool of (ID3D12CommandAllocator, ID3D12GraphicsCommandList) pairs for D3D12_COMMAND_LIST_TYPE_DIRECT.
 *
 * Why pooling matters:
 *   - CreateCommandAllocator and CreateCommandList are among the slowest D3D12 APIs (the runtime
 *     pre-patches command-list metadata against the device). Microsoft's guidance is "create
 *     once, reset many".
 *   - Backends that submit multiple command lists per frame (Tiled rendering, BackgroundBlur
 *     offscreen passes, transient upload lists) compound the cost.
 *
 * Lifecycle invariants:
 *   - acquire() returns a recording-state list. On a hit, both objects have been Reset(); on a
 *     miss, freshly-created (D3D12 returns lists in recording state by default).
 *   - release() is called by D3D12GPU::reclaimSubmission only after the GPU fence has confirmed
 *     execution completed. That guarantees ID3D12CommandAllocator::Reset is safe at the next
 *     acquire().
 *   - Abandoned sessions (encoder destroyed before submit) do not return to the pool; their
 *     ComPtrs simply destruct. Avoids any ambiguity about "is the list closed yet" at recycle
 *     time.
 *
 * Thread safety: not thread-safe. Caller serialises access (matches the rest of the D3D12
 * backend's single-threaded usage).
 */
class D3D12CommandListPool {
 public:
  // Soft cap on idle pairs kept around. Anything released past the cap is destroyed instead of
  // pooled, preventing unbounded growth in long-running applications. 16 is comfortably above
  // MAX_FRAMES_IN_FLIGHT (2) plus typical per-frame transient upload lists, so steady-state
  // workloads won't churn the cap.
  static constexpr size_t MAX_POOLED = 16;

  struct Entry {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    bool valid() const {
      return allocator != nullptr && commandList != nullptr;
    }
  };

  D3D12CommandListPool() = default;
  ~D3D12CommandListPool() = default;

  D3D12CommandListPool(const D3D12CommandListPool&) = delete;
  D3D12CommandListPool& operator=(const D3D12CommandListPool&) = delete;

  /**
   * Acquires a pair ready for recording. On a cache hit, both objects are Reset() before being
   * returned. On a miss (or if Reset fails), a fresh pair is created. Returns an Entry with both
   * fields null only if the underlying CreateXxx call fails, which is logged.
   */
  Entry acquire(ID3D12Device* device);

  /**
   * Returns a pair to the pool for future reuse. Caller must guarantee the GPU has finished
   * executing every command list that was recorded with this pair (i.e. the fence value
   * associated with the submission has signalled). Entries beyond MAX_POOLED are destroyed.
   */
  void release(Entry entry);

  /**
   * Drops every pooled pair. Used by D3D12GPU::releaseAll on shutdown to release driver
   * references before the device disappears.
   */
  void clear();

 private:
  std::vector<Entry> freeList;
};

}  // namespace tgfx
