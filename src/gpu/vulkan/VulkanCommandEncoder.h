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
#include "tgfx/gpu/CommandEncoder.h"

namespace tgfx {

class VulkanGPU;

/**
 * Records GPU commands into a VkCommandBuffer and tracks resource references.
 *
 * Vulkan command buffers are deferred: they execute on the GPU long after recording finishes. If a
 * resource's shared_ptr refcount drops to zero before the GPU completes execution, the underlying
 * Vulkan object would be destroyed while still in use. To prevent this, the encoder collects
 * shared_ptr references to every resource bound during recording (via retainResource()). These
 * references are transferred to VulkanCommandBuffer on finish(), then to InflightSubmission on
 * submit(), and finally released only after the fence signals GPU completion.
 *
 * Unlike Metal (which automatically retains encoded resources) or OpenGL (whose driver internally
 * reference-counts objects), Vulkan requires the application to manage this explicitly.
 */
class VulkanCommandEncoder : public CommandEncoder, public VulkanResource {
 public:
  static std::shared_ptr<VulkanCommandEncoder> Make(VulkanGPU* gpu);

  VkCommandBuffer vulkanCommandBuffer() const {
    return commandBuffer;
  }

  VkDescriptorPool vulkanDescriptorPool() const {
    return descriptorPool;
  }

  void addDeferredDestroy(VkRenderPass rp, VkFramebuffer fb) {
    deferredDestroys.push_back({rp, fb});
  }

  /// Keeps a strong reference to a resource that the command buffer will access on the GPU.
  /// Called at bind time (setPipeline, setTexture, setVertexBuffer, etc.) to ensure the resource
  /// survives until GPU execution completes.
  void retainResource(std::shared_ptr<VulkanResource> resource) {
    retainedResources.push_back(std::move(resource));
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
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

  struct DeferredDestroy {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
  };
  std::vector<DeferredDestroy> deferredDestroys;
  std::vector<std::shared_ptr<VulkanResource>> retainedResources;

  friend class VulkanGPU;
};

}  // namespace tgfx
