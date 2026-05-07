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
 * Immutable record of a finished command encoding session. Created by VulkanCommandEncoder::onFinish
 * which transfers all ownership here. Acts as the transport container between encoding and
 * submission: VulkanCommandQueue::submit() moves everything from here into an InflightSubmission
 * tied to a fence, after which this object is no longer referenced.
 *
 * Ownership chain: Encoder -> CommandBuffer -> InflightSubmission -> (fence signals) -> released.
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
  // Vulkan objects created per-frame (RenderPass/Framebuffer) that cannot be destroyed until GPU
  // execution completes. These are moved to InflightSubmission and destroyed after fence signals.
  std::vector<VkFramebuffer> _deferredFramebuffers;
  std::vector<VkRenderPass> _deferredRenderPasses;
  // Strong references preventing VulkanResource destruction while GPU is still executing.
  std::vector<std::shared_ptr<VulkanResource>> _retainedResources;
};

}  // namespace tgfx
