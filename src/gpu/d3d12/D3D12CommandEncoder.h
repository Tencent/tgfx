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
#include "D3D12FrameSession.h"
#include "D3D12Resource.h"
#include "D3D12Util.h"
#include "tgfx/gpu/CommandEncoder.h"

namespace tgfx {

class D3D12GPU;

/**
 * Records GPU commands into an ID3D12GraphicsCommandList and collects resource references into a
 * D3D12FrameSession.
 *
 * Lifecycle mirrors VulkanCommandEncoder:
 *   - Make() allocates an ID3D12CommandAllocator + ID3D12GraphicsCommandList (already in
 *     recording state per the D3D12 API) and binds the GPU's process-wide shader-visible
 *     CBV/SRV/UAV ring and Sampler heap to the list.
 *   - Resource binding (RenderPass) and copy commands populate retainedResources so the GPU
 *     keeps live references until the fence signals. Descriptor slots used during the session
 *     are sub-allocated from the GPU's descriptor ring and reclaimed by fence directly.
 *   - onFinish() Closes the command list and moves the entire session to D3D12CommandBuffer.
 *   - onRelease() (abandon path) reclaims the session via D3D12GPU::reclaimAbandonedSession().
 */
class D3D12CommandEncoder : public CommandEncoder, public D3D12Resource {
 public:
  static std::shared_ptr<D3D12CommandEncoder> Make(D3D12GPU* gpu);

  ID3D12GraphicsCommandList* d3d12CommandList() const {
    return session.commandList.Get();
  }

  ID3D12CommandAllocator* d3d12CommandAllocator() const {
    return session.commandAllocator.Get();
  }

  GPU* gpu() const override;

  std::shared_ptr<RenderPass> onBeginRenderPass(const RenderPassDescriptor& descriptor) override;

  void copyTextureToTexture(std::shared_ptr<Texture> srcTexture, const Rect& srcRect,
                            std::shared_ptr<Texture> dstTexture, const Point& dstOffset) override;

  void copyTextureToBuffer(std::shared_ptr<Texture> srcTexture, const Rect& srcRect,
                           std::shared_ptr<GPUBuffer> dstBuffer, size_t dstOffset = 0,
                           size_t dstRowBytes = 0) override;

  void generateMipmapsForTexture(std::shared_ptr<Texture> texture) override;

 protected:
  std::shared_ptr<CommandBuffer> onFinish() override;
  void onRelease(D3D12GPU* gpu) override;

 private:
  D3D12CommandEncoder(D3D12GPU* gpu, ComPtr<ID3D12CommandAllocator> allocator,
                      ComPtr<ID3D12GraphicsCommandList> commandList);
  ~D3D12CommandEncoder() override = default;

  D3D12GPU* _gpu = nullptr;
  D3D12FrameSession session;

  // Used by D3D12RenderPass to register attachments / pipelines / textures for deferred release.
  void retainResource(std::shared_ptr<D3D12Resource> resource) {
    session.retainedResources.push_back(std::move(resource));
  }

  void retainDescriptorHeap(ComPtr<ID3D12DescriptorHeap> heap) {
    session.retainedDescriptorHeaps.push_back(std::move(heap));
  }

  void retainRTVDSVHeap(ComPtr<ID3D12DescriptorHeap> heap) {
    session.retainedRTVDSVHeaps.push_back(std::move(heap));
  }

  friend class D3D12GPU;
  friend class D3D12RenderPass;
};

}  // namespace tgfx
