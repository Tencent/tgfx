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

#include <unordered_map>
#include <vector>
#include "D3D12Resource.h"
#include "D3D12Util.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {

class D3D12GPU;

/**
 * D3D12 render pipeline implementation. Owns three D3D12 objects produced from a single
 * RenderPipelineDescriptor:
 *
 *   1. Root signature  — equivalent to Vulkan's pipeline layout + descriptor set layout. Lays
 *                        out where uniform buffers, textures, and samplers bind in the root
 *                        argument table consumed by the command list.
 *   2. Pipeline state object (PSO) — fixed-function + shader configuration; bound once via
 *                        SetPipelineState() at the start of a draw sequence.
 *   3. Binding metadata — lookup tables that translate user-facing binding numbers (the same
 *                        numbers passed by GLSL programs) to root-parameter indices and texture
 *                        unit ordinals consumed by D3D12RenderPass.
 *
 * Root signature layout produced for every pipeline (matches the SPIR-V -> HLSL register
 * convention used by D3D12ShaderModule):
 *
 *   root parameter 0 : CBV  (b0)                             [VertexUniformBlock, optional]
 *   root parameter 1 : CBV  (b0)                             [FragmentUniformBlock, optional]
 *   root parameter 2..2N+1 : per texture sampler binding, two consecutive DescriptorTables —
 *                {SRV t{i}} then {Sampler s{i}}. SRV and Sampler descriptor tables
 *                            cannot share one root parameter because they reference different
 *                            descriptor heap types.
 *
 * ShaderVisibility for every root parameter comes from the BindingEntry's visibility field
 * (ToD3D12ShaderVisibility), not a fixed stage. UBO root parameters are CBVs with raw GPU
 * virtual addresses, allowing the command queue to dynamically supply per-draw uniform data
 * without re-allocating descriptor heaps.
 */
class D3D12RenderPipeline : public RenderPipeline, public D3D12Resource {
 public:
  static std::shared_ptr<D3D12RenderPipeline> Make(D3D12GPU* gpu,
                                                   const RenderPipelineDescriptor& descriptor);

  ID3D12RootSignature* d3d12RootSignature() const {
    return rootSignature.Get();
  }

  ID3D12PipelineState* d3d12PipelineState() const {
    return pipelineState.Get();
  }

  const std::vector<uint32_t>* getUniformRootParameterIndices(unsigned binding) const;

  /**
   * Returns the root-parameter index of the descriptor table holding the SRV for the given
   * texture-sampler binding, or UINT32_MAX if the binding is not present. The Sampler descriptor
   * table for the same binding is stored at the next consecutive root parameter and can be
   * obtained with getSamplerRootParameterIndex().
   */
  uint32_t getTextureRootParameterIndex(unsigned binding) const;

  /**
   * Returns the root-parameter index of the descriptor table holding the Sampler for the given
   * texture-sampler binding, or UINT32_MAX if the binding is not present.
   */
  uint32_t getSamplerRootParameterIndex(unsigned binding) const;

  /**
   * Returns the byte stride for the vertex buffer slot at the given index, as declared by the
   * pipeline's VertexBufferLayout. Returns 0 for slots that the pipeline does not consume.
   */
  uint32_t getVertexStride(unsigned slot) const {
    return slot < vertexStrides.size() ? vertexStrides[slot] : 0;
  }

 protected:
  void onRelease(D3D12GPU* gpu) override;

 private:
  D3D12RenderPipeline(D3D12GPU* gpu, const RenderPipelineDescriptor& descriptor);
  ~D3D12RenderPipeline() override = default;

  bool createRootSignature(D3D12GPU* gpu, const RenderPipelineDescriptor& descriptor);
  bool createPipelineState(D3D12GPU* gpu, const RenderPipelineDescriptor& descriptor);

  ComPtr<ID3D12RootSignature> rootSignature = nullptr;
  ComPtr<ID3D12PipelineState> pipelineState = nullptr;

  std::unordered_map<unsigned, std::vector<uint32_t>> uniformRootParameterIndices = {};
  std::unordered_map<unsigned, uint32_t> textureRootParameterIndex = {};
  std::unordered_map<unsigned, uint32_t> samplerRootParameterIndex = {};
  std::vector<uint32_t> vertexStrides = {};

  friend class D3D12GPU;
};

}  // namespace tgfx
