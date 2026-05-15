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
 *   - Allocates a pair of shader-visible heaps (CBV_SRV_UAV + SAMPLER) used by every draw inside
 *     this render pass. SetGraphicsRootDescriptorTable writes during draw point into these heaps.
 *   - Performs ClearRenderTargetView / ClearDepthStencilView for any attachment with
 *     LoadAction::Clear.
 *
 * On end:
 *   - Transitions color attachments back to COMMON so they can be sampled later. RTV/DSV heaps
 *     and shader-visible heaps remain alive in the FrameSession until the fence signals.
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
  bool allocateShaderVisibleHeaps();

  // Lazily writes any pending texture/uniform bindings to the shader-visible descriptor heaps and
  // calls SetGraphicsRootConstantBufferView / SetGraphicsRootDescriptorTable as appropriate.
  // Called from draw() / drawIndexed() before the actual draw command.
  void flushBindingsIfNeeded();

  D3D12CommandEncoder* encoder = nullptr;
  D3D12GPU* d3d12GPU = nullptr;
  ID3D12GraphicsCommandList* commandList = nullptr;

  // Shader-visible descriptor heaps used to back SetGraphicsRootDescriptorTable. One CBV/SRV/UAV
  // heap and one Sampler heap, sized by NumTextureSlots * MaxTextureSlots; the encoder retains
  // them so they outlive GPU execution.
  ComPtr<ID3D12DescriptorHeap> srvHeap = nullptr;
  ComPtr<ID3D12DescriptorHeap> samplerHeap = nullptr;
  UINT srvDescriptorSize = 0;
  UINT samplerDescriptorSize = 0;
  // Linearly growing tail indices into the heaps. setTexture() advances them by one each.
  uint32_t srvHeapHead = 0;
  uint32_t samplerHeapHead = 0;
  // Total descriptor slots reserved at allocation time. Bindings beyond this fail with an error.
  uint32_t srvHeapCapacity = 0;
  uint32_t samplerHeapCapacity = 0;
  bool descriptorHeapsBoundToCmdList = false;

  // Per-render-pass dedup caches. Repeated setTexture() calls with the same texture+format or
  // sampler reuse the previously-allocated heap slot, so the heaps see one descriptor per unique
  // resource instead of one per draw call. This is critical for the Sampler heap because D3D12
  // caps the total live sampler descriptors per shader-visible heap at 2048.
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
  std::unordered_map<const void*, D3D12_GPU_DESCRIPTOR_HANDLE> samplerSlotCache;

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
  std::shared_ptr<D3D12Texture> depthStencilAttachment;
  // Textures that were transitioned to PIXEL_SHADER_RESOURCE inside this pass via setTexture().
  // We keep them tracked so that onEnd() can transition them back to COMMON, avoiding mismatches
  // with D3D12's automatic state-decay rules between ExecuteCommandLists calls.
  std::vector<std::shared_ptr<D3D12Texture>> shaderResourceTextures;
};

}  // namespace tgfx
