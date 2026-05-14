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

#include <vector>
#include "gpu/vulkan/VulkanAPI.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {

class VulkanCommandEncoder;
class VulkanGPU;
class VulkanRenderPipeline;

/**
 * Vulkan render pass implementation.
 */
class VulkanRenderPass : public RenderPass {
 public:
  static std::shared_ptr<VulkanRenderPass> Make(VulkanCommandEncoder* encoder,
                                                const RenderPassDescriptor& descriptor);

  ~VulkanRenderPass() override;

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
  VulkanRenderPass(VulkanCommandEncoder* encoder, VulkanGPU* gpu,
                   const RenderPassDescriptor& descriptor);

  void bindDescriptorSetIfDirty();

  VulkanCommandEncoder* encoder = nullptr;
  VulkanGPU* vulkanGPU = nullptr;
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;

  struct UniformBinding {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
  };

  struct TextureBinding {
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
  };

  struct VertexBinding {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
  };

  // Tracks the most recently bound state to skip redundant Vulkan commands when consecutive
  // draws use the same pipeline, buffers, textures, or scissor rect.
  struct BoundState {
    std::shared_ptr<VulkanRenderPipeline> pipeline = nullptr;
    std::vector<UniformBinding> uniformBindings;
    std::vector<TextureBinding> textureBindings;
    std::vector<VertexBinding> vertexBindings;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkIndexType indexType = VK_INDEX_TYPE_UINT16;
    int scissorX = 0;
    int scissorY = 0;
    int scissorWidth = -1;
    int scissorHeight = -1;
    bool descriptorDirty = false;
    // The actual VkPipeline handle currently bound to the command buffer. Used when
    // extendedDynamicState is unavailable to detect whether a topology-variant rebind is needed.
    VkPipeline boundVkPipeline = VK_NULL_HANDLE;
  };

  BoundState lastBound;
};

}  // namespace tgfx
