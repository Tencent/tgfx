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
#include "core/utils/Log.h"
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

  VkDescriptorSetLayout vulkanDescriptorSetLayout() const {
    return descriptorSetLayout;
  }

  unsigned getTextureIndex(unsigned binding) const;

  unsigned getDescriptorBinding(unsigned binding) const;

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
  void onRelease(VulkanGPU* gpu) override;

 private:
  VulkanRenderPipeline(VulkanGPU* gpu, const RenderPipelineDescriptor& descriptor);
  ~VulkanRenderPipeline() override = default;

  bool createDescriptorSetLayout(VulkanGPU* gpu, const RenderPipelineDescriptor& descriptor);
  bool createPipelineLayout(VulkanGPU* gpu);
  bool createPipeline(VulkanGPU* gpu, const RenderPipelineDescriptor& descriptor,
                      VkPrimitiveTopology topology, VkPipeline* outPipeline);

  VkPipeline pipeline = VK_NULL_HANDLE;
  VkPipeline stripPipeline = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  std::unordered_map<unsigned, unsigned> textureUnits = {};
  std::unordered_map<unsigned, unsigned> textureDescriptorBindings = {};
  std::unordered_map<unsigned, uint32_t> uniformBlockVisibility = {};
  std::unordered_set<unsigned> uniformBindingSet = {};
  std::unordered_set<unsigned> textureBindingSet = {};

  friend class VulkanGPU;
};

}  // namespace tgfx
