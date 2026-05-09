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
#include "gpu/vulkan/VulkanFrameSession.h"
#include "gpu/vulkan/VulkanResource.h"
#include "tgfx/gpu/CommandEncoder.h"

namespace tgfx {

class VulkanGPU;

/**
 * Records GPU commands into a VkCommandBuffer and collects resource references into a FrameSession.
 *
 * During recording, VulkanRenderPass calls retainResource() and addDeferredDestroy() to register
 * resources that the command buffer will reference on the GPU. These are stored locally in the
 * encoder's FrameSession — not pushed to any external container — so that abandon (encoder
 * destroyed without calling finish()) can clean up without coordinating with other classes.
 *
 * On finish(), the entire FrameSession is moved to VulkanCommandBuffer in a single O(1) operation.
 * On abandon (onRelease()), the encoder destroys deferred objects and releases retained references
 * directly — safe because the command buffer was never submitted to the GPU.
 */
class VulkanCommandEncoder : public CommandEncoder, public VulkanResource {
 public:
  static std::shared_ptr<VulkanCommandEncoder> Make(VulkanGPU* gpu);

  VkCommandBuffer vulkanCommandBuffer() const {
    return session.commandBuffer;
  }

  VkDescriptorPool vulkanDescriptorPool() const {
    return session.descriptorPool;
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
  void onRelease(VulkanGPU* gpu) override;

 private:
  VulkanCommandEncoder(VulkanGPU* gpu, VkCommandBuffer commandBuffer, VkCommandPool commandPool);
  ~VulkanCommandEncoder() override = default;

  VulkanGPU* _gpu = nullptr;
  FrameSession session;

  // Produced by VulkanRenderPass (via addDeferredDestroy()), consumed by onFinish() which moves
  // the session to VulkanCommandBuffer. Destroyed after fence signals GPU completion.
  void addDeferredDestroy(VkRenderPass rp, VkFramebuffer fb) {
    session.deferredRenderPasses.push_back(rp);
    session.deferredFramebuffers.push_back(fb);
  }

  // Produced by VulkanRenderPass (via retainResource()), consumed by onFinish() which moves
  // the session to VulkanCommandBuffer. Prevents resource destruction while GPU is executing.
  void retainResource(std::shared_ptr<VulkanResource> resource) {
    session.retainedResources.push_back(std::move(resource));
  }

  friend class VulkanGPU;
  friend class VulkanRenderPass;
};

}  // namespace tgfx
