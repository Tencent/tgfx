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
#include <unordered_set>
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
 *   root parameter 0 : CBV  (b0, visibility = Vertex)        [VertexUniformBlock, optional]
 *   root parameter 1 : CBV  (b0, visibility = Pixel)         [FragmentUniformBlock, optional]
 *   root parameter 2..N+1 : DescriptorTable {SRV t{i}, Sampler s{i}, visibility = Pixel}
 *                                                            [one per texture sampler binding]
 *
 * UBO root parameters are CBVs with raw GPU virtual addresses, allowing the command queue to
 * dynamically supply per-draw uniform data without re-allocating descriptor heaps.
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

  /**
   * Returns the root-parameter index that holds the CBV for the given uniform-block binding,
   * or UINT32_MAX if the binding is not present in the pipeline.
   */
  uint32_t getUniformRootParameterIndex(unsigned binding) const;

  /**
   * Returns the root-parameter index of the descriptor table that holds the SRV+Sampler pair
   * for the given texture-sampler binding, or UINT32_MAX if the binding is not present.
   */
  uint32_t getTextureRootParameterIndex(unsigned binding) const;

  /**
   * Returns the dense 0-based texture unit index for a texture-sampler binding. Mirrors the
   * VulkanRenderPipeline accessor used by RenderPass to map binding -> shader register.
   */
  unsigned getTextureIndex(unsigned binding) const;

  /**
   * Returns the visibility bitmask (ShaderVisibility::*) declared by the user for a uniform-block
   * binding, or ShaderVisibility::VertexFragment if unspecified.
   */
  uint32_t getUniformBlockVisibility(unsigned binding) const;

  bool hasUniformBinding(unsigned binding) const {
    return uniformBindingSet.count(binding) > 0;
  }

  bool hasTextureBinding(unsigned binding) const {
    return textureBindingSet.count(binding) > 0;
  }

  const std::unordered_set<unsigned>& getTextureBindings() const {
    return textureBindingSet;
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

  std::unordered_map<unsigned, uint32_t> uniformRootParameterIndex = {};
  std::unordered_map<unsigned, uint32_t> textureRootParameterIndex = {};
  std::unordered_map<unsigned, unsigned> textureUnits = {};
  std::unordered_map<unsigned, uint32_t> uniformBlockVisibility = {};
  std::unordered_set<unsigned> uniformBindingSet = {};
  std::unordered_set<unsigned> textureBindingSet = {};

  friend class D3D12GPU;
};

}  // namespace tgfx
