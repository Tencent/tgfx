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
  VulkanRenderPass(VulkanCommandEncoder* encoder, const RenderPassDescriptor& descriptor);

  void bindDescriptorSetIfDirty();

  VulkanCommandEncoder* encoder = nullptr;
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  std::shared_ptr<VulkanRenderPipeline> currentPipeline = nullptr;
  std::shared_ptr<GPUBuffer> currentIndexBuffer = nullptr;
  VkIndexType currentIndexType = VK_INDEX_TYPE_UINT16;

  static constexpr uint32_t MAX_DESCRIPTOR_SETS = 64;
  static constexpr uint32_t MAX_UNIFORM_BUFFERS = 128;
  static constexpr uint32_t MAX_COMBINED_SAMPLERS = 128;

  struct UniformBinding {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
  };

  struct TextureBinding {
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
  };

  std::vector<UniformBinding> uniformBindings;
  std::vector<TextureBinding> textureBindings;
  bool descriptorDirty = false;
};

}  // namespace tgfx
