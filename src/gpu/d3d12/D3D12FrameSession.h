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
#include "gpu/d3d12/D3D12Resource.h"
#include "gpu/d3d12/D3D12Util.h"

namespace tgfx {

/**
 * Value-type aggregate of all per-frame GPU resources produced during one encoding session.
 *
 * Why D3D12 needs this (mirrors VulkanFrameSession's role):
 *   - D3D12, like Vulkan, provides NO automatic resource tracking. ID3D12GraphicsCommandList is
 *     deferred: it executes on the GPU long after recording finishes. If an ID3D12Resource backing
 *     a buffer or texture is released before the GPU finishes reading it, behaviour is undefined.
 *     Likewise, ID3D12CommandAllocator must outlive every command list it produced until the GPU
 *     consumes them, otherwise Reset() will fail or recorded commands will be corrupted.
 *   - The application must explicitly keep allocator, command list, and any referenced resources
 *     alive until the associated ID3D12Fence value signals on the queue.
 *
 * D3D12FrameSession is the single place where "everything this frame needs" is defined. It is
 * moved (not copied) through the pipeline: Encoder -> CommandBuffer -> InflightSubmission. Cleanup
 * happens exclusively after the queue's fence confirms GPU completion.
 *
 * Differences from Vulkan's FrameSession:
 *   - No descriptor pools: D3D12 binds via descriptor heaps, which are managed independently and
 *     do not require per-frame pool churn.
 *   - No render passes / framebuffers: D3D12 has no equivalent persistent objects; render targets
 *     are bound through RTV/DSV descriptors at record time.
 *   - retainedResources holds D3D12Resource subclasses (buffers, textures, samplers, pipelines).
 *
 * Adding a new per-frame resource type requires only two changes:
 *   1. Add a field here.
 *   2. Add cleanup logic in D3D12GPU::reclaimSubmission() (introduced in a later step).
 */
struct D3D12FrameSession {
  ComPtr<ID3D12CommandAllocator> commandAllocator;
  ComPtr<ID3D12GraphicsCommandList> commandList;
  // Strong references preventing D3D12Resource destruction while GPU is still executing.
  // When cleared after the fence signals, refcounts decrement; resources reaching zero enter the
  // ReturnQueue and are safely destroyed during processUnreferencedResources().
  std::vector<std::shared_ptr<D3D12Resource>> retainedResources;
  // Shader-visible descriptor heaps (CBV/SRV/UAV and Sampler) created per render pass to back
  // SetGraphicsRootDescriptorTable. They must outlive GPU execution because the GPU keeps reading
  // their contents until the fence signals. Released after the fence signals.
  std::vector<ComPtr<ID3D12DescriptorHeap>> retainedDescriptorHeaps;
  // RTV/DSV heaps and any other auxiliary D3D12 objects that must outlive GPU execution but do not
  // fit into retainedResources (which only holds D3D12Resource subclasses). Released after fence
  // signals together with retainedDescriptorHeaps.
  std::vector<ComPtr<ID3D12DescriptorHeap>> retainedRTVDSVHeaps;
  // Auxiliary command allocators/lists used to record one-off work (texture uploads) outside the
  // main command list. Captured here so they outlive GPU execution; freed after the fence signals.
  std::vector<ComPtr<ID3D12CommandAllocator>> auxAllocators;
  std::vector<ComPtr<ID3D12GraphicsCommandList>> auxCommandLists;
  // Auxiliary ID3D12Resource buffers (e.g. transient staging buffers used by
  // copyTextureToBuffer for row-pitch alignment) that must live until the fence signals.
  std::vector<ComPtr<ID3D12Resource>> auxBuffers;

  D3D12FrameSession() = default;

  // ComPtr is move-aware and zeroes the source on move; std::vector moves are also clean. The
  // explicit move/copy declarations match VulkanFrameSession for consistency and to make the
  // copy-deletion intent obvious at the call site.
  D3D12FrameSession(D3D12FrameSession&& other) noexcept = default;
  D3D12FrameSession& operator=(D3D12FrameSession&& other) noexcept = default;

  D3D12FrameSession(const D3D12FrameSession&) = delete;
  D3D12FrameSession& operator=(const D3D12FrameSession&) = delete;
};

}  // namespace tgfx
