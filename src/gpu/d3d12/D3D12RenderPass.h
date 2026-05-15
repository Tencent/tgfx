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
#include <unordered_map>
#include <vector>
#include "D3D12Util.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {

class D3D12CommandEncoder;
class D3D12GPU;
class D3D12RenderPipeline;
class D3D12Texture;

/**
 * D3D12 render pass implementation.
 *
 * On construction:
 *   - Allocates per-pass non-shader-visible RTV/DSV heaps and creates one descriptor per
 *     attachment. Issues ResourceBarrier transitions and OMSetRenderTargets.
 *   - Performs ClearRenderTargetView / ClearDepthStencilView for any attachment with
 *     LoadAction::Clear.
 *
 * Texture/sampler bindings:
 *   - Sub-allocates SRV slots out of the GPU's process-wide D3D12DescriptorRing (committed and
 *     fence-retired around each submission). No CreateDescriptorHeap call per render pass.
 *   - Reuses each D3D12Sampler's stable GPU descriptor handle from the GPU's append-only
 *     shader-visible Sampler heap; no per-binding CreateSampler is issued either.
 *   - SetDescriptorHeaps was already issued once on the encoder's command list, so render passes
 *     never need to call it.
 *
 * On end:
 *   - Transitions color attachments back to COMMON so they can be sampled later. RTV/DSV heaps
 *     remain alive in the FrameSession until the fence signals.
 */
class D3D12RenderPass : public RenderPass {
 public:
  static std::shared_ptr<D3D12RenderPass> Make(D3D12CommandEncoder* encoder,
                                               const RenderPassDescriptor& descriptor);

  ~D3D12RenderPass() override = default;

  GPU* gpu() const override;
  void setViewport(int x, int y, int width, int height) override;
  void setScissorRect(int x, int y, int width, int height) override;
  void setPipeline(std::shared_ptr<RenderPipeline> pipeline) override;
  void setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer, size_t offset,
                        size_t size) override;
  void setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                  std::shared_ptr<Sampler> sampler) override;
  void setVertexBuffer(unsigned slot, std::shared_ptr<GPUBuffer> buffer,
                       size_t offset = 0) override;
  void setIndexBuffer(std::shared_ptr<GPUBuffer> buffer,
                      IndexFormat format = IndexFormat::UInt16) override;
  void setStencilReference(uint32_t reference) override;
  void draw(PrimitiveType primitiveType, uint32_t vertexCount, uint32_t instanceCount = 1,
            uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
  void drawIndexed(PrimitiveType primitiveType, uint32_t indexCount, uint32_t instanceCount = 1,
                   uint32_t firstIndex = 0, int32_t baseVertex = 0,
                   uint32_t firstInstance = 0) override;

 protected:
  void onEnd() override;

 private:
  D3D12RenderPass(D3D12CommandEncoder* encoder, D3D12GPU* gpu,
                  const RenderPassDescriptor& descriptor);

  bool initialise(const RenderPassDescriptor& descriptor);

  // Lazily writes any pending texture/uniform bindings to the shader-visible descriptor heaps and
  // calls SetGraphicsRootConstantBufferView / SetGraphicsRootDescriptorTable as appropriate.
  // Called from draw() / drawIndexed() before the actual draw command.
  void flushBindingsIfNeeded();

  D3D12CommandEncoder* encoder = nullptr;
  D3D12GPU* d3d12GPU = nullptr;
  ID3D12GraphicsCommandList* commandList = nullptr;

  // Per-render-pass dedup cache for SRV slots in the GPU's shader-visible CBV/SRV/UAV ring.
  // Repeated bindings of the same (resource, format, mipLevels) within a single pass share one
  // sub-allocated descriptor; the cache is cleared at the next pass start because ring slots may
  // have been retired by then.
  struct SrvCacheKey {
    ID3D12Resource* resource = nullptr;
    DXGI_FORMAT format = static_cast<DXGI_FORMAT>(0);  // DXGI_FORMAT_UNKNOWN
    UINT mipLevels = 0;
    bool operator==(const SrvCacheKey& other) const {
      return resource == other.resource && format == other.format && mipLevels == other.mipLevels;
    }
  };
  struct SrvCacheKeyHash {
    size_t operator()(const SrvCacheKey& k) const noexcept {
      auto h1 = std::hash<void*>{}(static_cast<void*>(k.resource));
      auto h2 = std::hash<UINT>{}(static_cast<UINT>(k.format));
      auto h3 = std::hash<UINT>{}(k.mipLevels);
      return h1 ^ (h2 * 0x9E3779B97F4A7C15ull) ^ (h3 * 0xBF58476D1CE4E5B9ull);
    }
  };
  std::unordered_map<SrvCacheKey, D3D12_GPU_DESCRIPTOR_HANDLE, SrvCacheKeyHash> srvSlotCache;

  // Per-binding deferred state. setUniformBuffer / setTexture record the argument; the actual
  // RootCBV / RootDescriptorTable bind happens at flushBindingsIfNeeded() (just before draw).
  struct UniformBinding {
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
    bool dirty = false;
  };
  struct TextureBinding {
    D3D12_GPU_DESCRIPTOR_HANDLE srvTableStart = {};
    D3D12_GPU_DESCRIPTOR_HANDLE samplerTableStart = {};
    bool dirty = false;
  };
  static constexpr unsigned MaxUniformBindings = 8;
  static constexpr unsigned MaxTextureBindings = 32;
  UniformBinding uniformBindings[MaxUniformBindings] = {};
  TextureBinding textureBindings[MaxTextureBindings] = {};

  std::shared_ptr<D3D12RenderPipeline> currentPipeline = nullptr;
  D3D12_PRIMITIVE_TOPOLOGY currentTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
  bool primitiveTopologySet = false;

  // Color attachments retained for state-restore at onEnd() time.
  std::vector<std::shared_ptr<D3D12Texture>> colorAttachments;
  // Per-color-attachment MSAA resolve target. Index N corresponds to colorAttachments[N]; an
  // entry is nullptr when the matching color attachment has no resolve texture (i.e. sampleCount
  // == 1). At onEnd() time we issue ResolveSubresource(colorAttachments[N], resolveTextures[N])
  // for every non-null pair, mirroring Vulkan's pResolveAttachments behaviour.
  std::vector<std::shared_ptr<D3D12Texture>> resolveTextures;
  std::shared_ptr<D3D12Texture> depthStencilAttachment;
  // Textures that were transitioned to PIXEL_SHADER_RESOURCE inside this pass via setTexture().
  // We keep them tracked so that onEnd() can transition them back to COMMON, avoiding mismatches
  // with D3D12's automatic state-decay rules between ExecuteCommandLists calls.
  std::vector<std::shared_ptr<D3D12Texture>> shaderResourceTextures;
};

}  // namespace tgfx
