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
#include "gpu/vulkan/VulkanAPI.h"
#include "gpu/vulkan/VulkanResource.h"
#include "tgfx/gpu/CommandBuffer.h"

namespace tgfx {

/**
 * Vulkan command buffer implementation. Owns the command pool, deferred GPU objects, and retained
 * resource references that must outlive command buffer execution on the GPU.
 */
class VulkanCommandBuffer : public CommandBuffer {
 public:
  VulkanCommandBuffer(VkCommandBuffer commandBuffer, VkCommandPool commandPool,
                      VkDescriptorPool descriptorPool, std::vector<VkFramebuffer> framebuffers,
                      std::vector<VkRenderPass> renderPasses,
                      std::vector<std::shared_ptr<VulkanResource>> retainedResources);
  ~VulkanCommandBuffer() override = default;

  VkCommandBuffer vulkanCommandBuffer() const {
    return commandBuffer;
  }

  VkCommandPool vulkanCommandPool() const {
    return commandPool;
  }

  VkDescriptorPool descriptorPool() const {
    return _descriptorPool;
  }

  std::vector<VkFramebuffer>& deferredFramebuffers() {
    return _deferredFramebuffers;
  }

  std::vector<VkRenderPass>& deferredRenderPasses() {
    return _deferredRenderPasses;
  }

  std::vector<std::shared_ptr<VulkanResource>>& retainedResources() {
    return _retainedResources;
  }

 private:
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> _deferredFramebuffers;
  std::vector<VkRenderPass> _deferredRenderPasses;
  std::vector<std::shared_ptr<VulkanResource>> _retainedResources;
};

}  // namespace tgfx
