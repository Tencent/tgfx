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

namespace tgfx {

/**
 * Value-type aggregate of all per-frame GPU resources produced during one encoding session.
 *
 * Why Vulkan needs this (unlike GL/Metal):
 *   - OpenGL defers destruction internally — when the app calls glDeleteTextures/glDeleteBuffers,
 *     the driver keeps the underlying object alive until all pending commands referencing it have
 *     completed. The app never needs to track GPU-side lifetimes explicitly.
 *   - Metal uses ARC + command buffer autorelease — the driver internally retains every resource
 *     encoded into a command buffer and releases them after GPU completion. The app does nothing.
 *   - Vulkan provides NO automatic resource tracking. Command buffers are deferred: they execute
 *     on the GPU long after recording finishes. If a VkPipeline/VkBuffer/VkImage is destroyed
 *     before the GPU finishes reading it, the result is undefined behavior (crash or corruption).
 *     The application must explicitly keep resources alive until the associated VkFence signals.
 *
 * FrameSession is the single place where "everything this frame needs" is defined. It is moved
 * (not copied) through the pipeline: Encoder -> CommandBuffer -> InflightSubmission. Cleanup
 * happens exclusively in VulkanGPU::reclaimSubmission() after the fence confirms GPU completion.
 *
 * Adding a new per-frame resource type requires only two changes:
 *   1. Add a field here.
 *   2. Add cleanup logic in VulkanGPU::reclaimSubmission().
 */
struct FrameSession {
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  // Chain of descriptor pools used this frame. Normally only one; additional pools are acquired
  // on demand when the current pool is exhausted (see VulkanCommandEncoder::allocateDescriptorSet).
  std::vector<VkDescriptorPool> descriptorPools;
  // Vulkan objects created per-frame that must outlive GPU execution. Destroyed after fence signals.
  std::vector<VkFramebuffer> deferredFramebuffers;
  std::vector<VkRenderPass> deferredRenderPasses;
  // Strong references preventing VulkanResource destruction while GPU is still executing.
  // When cleared after fence signals, refcounts decrement; resources reaching zero enter the
  // ReturnQueue and are safely destroyed during processUnreferencedResources().
  std::vector<std::shared_ptr<VulkanResource>> retainedResources;

  FrameSession() = default;

  // Move constructor zeroes source handles to prevent double-destroy. Vectors are naturally emptied.
  FrameSession(FrameSession&& other) noexcept
      : commandPool(other.commandPool), commandBuffer(other.commandBuffer),
        descriptorPools(std::move(other.descriptorPools)),
        deferredFramebuffers(std::move(other.deferredFramebuffers)),
        deferredRenderPasses(std::move(other.deferredRenderPasses)),
        retainedResources(std::move(other.retainedResources)) {
    other.commandPool = VK_NULL_HANDLE;
    other.commandBuffer = VK_NULL_HANDLE;
  }

  // Move assignment zeroes source handles to prevent double-destroy.
  FrameSession& operator=(FrameSession&& other) noexcept {
    if (this != &other) {
      commandPool = other.commandPool;
      commandBuffer = other.commandBuffer;
      descriptorPools = std::move(other.descriptorPools);
      deferredFramebuffers = std::move(other.deferredFramebuffers);
      deferredRenderPasses = std::move(other.deferredRenderPasses);
      retainedResources = std::move(other.retainedResources);
      other.commandPool = VK_NULL_HANDLE;
      other.commandBuffer = VK_NULL_HANDLE;
    }
    return *this;
  }

  FrameSession(const FrameSession&) = delete;
  FrameSession& operator=(const FrameSession&) = delete;
};

}  // namespace tgfx
