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

#include <map>
#include <unordered_map>
#include <unordered_set>
#include "core/utils/Log.h"
#include "gpu/VaryingShaderModule.h"
#include "gpu/vulkan/VulkanAPI.h"
#include "gpu/vulkan/VulkanResource.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {

class VulkanGPU;

/**
 * Vulkan render pipeline implementation.
 */
class VulkanRenderPipeline : public RenderPipeline, public VulkanResource {
 public:
  static std::shared_ptr<VulkanRenderPipeline> Make(VulkanGPU* gpu,
                                                    const RenderPipelineDescriptor& descriptor);

  VkPipeline vulkanPipeline() const {
    return pipeline;
  }

  /// Returns the VkPipeline matching the given primitive type. When extendedDynamicState is
  /// unavailable, a separate TriangleStrip pipeline variant is created at construction time.
  VkPipeline vulkanPipeline(PrimitiveType type) const {
    if (type == PrimitiveType::TriangleStrip) {
      DEBUG_ASSERT(stripPipeline != VK_NULL_HANDLE);
      return stripPipeline;
    }
    return pipeline;
  }

  VkPipelineLayout vulkanPipelineLayout() const {
    return pipelineLayout;
  }

  VkDescriptorSetLayout vulkanVertexUboSetLayout() const {
    return vertexUboSetLayout;
  }

  VkDescriptorSetLayout vulkanFragmentUboSetLayout() const {
    return fragmentUboSetLayout;
  }

  VkDescriptorSetLayout vulkanTextureSetLayout() const {
    return textureSetLayout;
  }

  unsigned getTextureIndex(unsigned binding) const;

  /// Returns the per-stage physical UBO slots for the given public logical binding, or nullptr if
  /// the pipeline does not use that binding.
  const UniformSlotMapping* getUniformSlots(unsigned binding) const;

  const std::unordered_set<unsigned>& getTextureBindings() const {
    return textureBindingSet;
  }

 protected:
  void onRelease(VulkanGPU* gpu) override;

 private:
  VulkanRenderPipeline(VulkanGPU* gpu, const RenderPipelineDescriptor& descriptor);
  ~VulkanRenderPipeline() override = default;

  bool createDescriptorSetLayouts(VulkanGPU* gpu, const RenderPipelineDescriptor& descriptor);
  bool createPipelineLayout(VulkanGPU* gpu);
  bool createPipeline(VulkanGPU* gpu, const RenderPipelineDescriptor& descriptor,
                      VkPrimitiveTopology topology, VkPipeline* outPipeline);

  VkPipeline pipeline = VK_NULL_HANDLE;
  VkPipeline stripPipeline = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout vertexUboSetLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout fragmentUboSetLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout textureSetLayout = VK_NULL_HANDLE;
  std::unordered_map<unsigned, unsigned> textureUnits = {};
  std::map<unsigned, UniformSlotMapping> uniformSlots = {};
  std::unordered_set<unsigned> textureBindingSet = {};

  friend class VulkanGPU;
};

}  // namespace tgfx
