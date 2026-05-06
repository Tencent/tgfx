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

#include "VulkanRenderPass.h"
#include "VulkanBuffer.h"
#include "VulkanCommandEncoder.h"
#include "VulkanGPU.h"
#include "VulkanRenderPipeline.h"
#include "VulkanSampler.h"
#include "VulkanTexture.h"
#include "VulkanUtil.h"
#include "core/utils/Log.h"

namespace tgfx {

static VkAttachmentLoadOp ToVkLoadOp(LoadAction action) {
  switch (action) {
    case LoadAction::Clear:
      return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadAction::Load:
      return VK_ATTACHMENT_LOAD_OP_LOAD;
    default:
      return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  }
}

static VkAttachmentStoreOp ToVkStoreOp(StoreAction action) {
  switch (action) {
    case StoreAction::Store:
      return VK_ATTACHMENT_STORE_OP_STORE;
    default:
      return VK_ATTACHMENT_STORE_OP_DONT_CARE;
  }
}

static void TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout,
                                  VkImageLayout newLayout, VkImageAspectFlags aspectMask) {
  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = aspectMask;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &barrier);
}

std::shared_ptr<VulkanRenderPass> VulkanRenderPass::Make(VulkanCommandEncoder* encoder,
                                                         const RenderPassDescriptor& descriptor) {
  if (!encoder) {
    return nullptr;
  }
  auto vulkanGPU = static_cast<VulkanGPU*>(encoder->gpu());
  return std::shared_ptr<VulkanRenderPass>(new VulkanRenderPass(encoder, vulkanGPU, descriptor));
}

VulkanRenderPass::VulkanRenderPass(VulkanCommandEncoder* encoder, VulkanGPU* gpu,
                                   const RenderPassDescriptor& passDescriptor)
    : RenderPass(passDescriptor), encoder(encoder), vulkanGPU(gpu),
      commandBuffer(encoder->vulkanCommandBuffer()) {

  auto device = vulkanGPU->device();

  uniformBindings.resize(16);
  textureBindings.resize(16);

  std::vector<VkAttachmentDescription> attachments;
  std::vector<VkAttachmentReference> colorRefs;
  std::vector<VkImageView> fbAttachments;
  uint32_t fbWidth = 0;
  uint32_t fbHeight = 0;
  std::vector<VkClearValue> clearValues;

  for (auto& ca : passDescriptor.colorAttachments) {
    if (!ca.texture) {
      continue;
    }
    auto vulkanTexture = std::static_pointer_cast<VulkanTexture>(ca.texture);

    // Transition color attachment to GENERAL layout before render pass begins. We use GENERAL for
    // all color attachments to maintain a uniform layout across the backend, avoiding the
    // complexity of tracking per-image optimal layouts.
    TransitionImageLayout(commandBuffer, vulkanTexture->vulkanImage(),
                          vulkanTexture->currentLayout(), VK_IMAGE_LAYOUT_GENERAL,
                          VK_IMAGE_ASPECT_COLOR_BIT);

    VkAttachmentDescription attachment = {};
    attachment.format = vulkanTexture->vulkanFormat();
    attachment.samples = static_cast<VkSampleCountFlagBits>(ca.texture->sampleCount());
    attachment.loadOp = ToVkLoadOp(ca.loadAction);
    attachment.storeOp = ToVkStoreOp(ca.storeAction);
    attachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

    vulkanTexture->setCurrentLayout(VK_IMAGE_LAYOUT_GENERAL);

    colorRefs.push_back({static_cast<uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_GENERAL});
    attachments.push_back(attachment);
    fbAttachments.push_back(vulkanTexture->vulkanImageView());

    VkClearValue clearValue = {};
    clearValue.color = {
        {ca.clearValue.red, ca.clearValue.green, ca.clearValue.blue, ca.clearValue.alpha}};
    clearValues.push_back(clearValue);

    fbWidth = static_cast<uint32_t>(ca.texture->width());
    fbHeight = static_cast<uint32_t>(ca.texture->height());
  }

  VkAttachmentReference depthRef = {};
  bool hasDepth = (passDescriptor.depthStencilAttachment.texture != nullptr);
  if (hasDepth) {
    auto dsTexture =
        std::static_pointer_cast<VulkanTexture>(passDescriptor.depthStencilAttachment.texture);

    TransitionImageLayout(commandBuffer, dsTexture->vulkanImage(), dsTexture->currentLayout(),
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = dsTexture->vulkanFormat();
    depthAttachment.samples = static_cast<VkSampleCountFlagBits>(dsTexture->sampleCount());
    depthAttachment.loadOp = ToVkLoadOp(passDescriptor.depthStencilAttachment.loadAction);
    depthAttachment.storeOp = ToVkStoreOp(passDescriptor.depthStencilAttachment.storeAction);
    depthAttachment.stencilLoadOp = ToVkLoadOp(passDescriptor.depthStencilAttachment.loadAction);
    depthAttachment.stencilStoreOp = ToVkStoreOp(passDescriptor.depthStencilAttachment.storeAction);
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    dsTexture->setCurrentLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    depthRef = {static_cast<uint32_t>(attachments.size()),
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    attachments.push_back(depthAttachment);
    fbAttachments.push_back(dsTexture->vulkanImageView());

    VkClearValue dsClear = {};
    dsClear.depthStencil = {passDescriptor.depthStencilAttachment.depthClearValue,
                            passDescriptor.depthStencilAttachment.stencilClearValue};
    clearValues.push_back(dsClear);
  }

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
  subpass.pColorAttachments = colorRefs.data();
  subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

  VkRenderPassCreateInfo rpInfo = {};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  rpInfo.pAttachments = attachments.data();
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;

  vkCreateRenderPass(device, &rpInfo, nullptr, &renderPass);

  VkFramebufferCreateInfo fbInfo = {};
  fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fbInfo.renderPass = renderPass;
  fbInfo.attachmentCount = static_cast<uint32_t>(fbAttachments.size());
  fbInfo.pAttachments = fbAttachments.data();
  fbInfo.width = fbWidth;
  fbInfo.height = fbHeight;
  fbInfo.layers = 1;

  vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffer);

  VkRenderPassBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  beginInfo.renderPass = renderPass;
  beginInfo.framebuffer = framebuffer;
  beginInfo.renderArea = {{0, 0}, {fbWidth, fbHeight}};
  beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  beginInfo.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport = {};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(fbWidth);
  viewport.height = static_cast<float>(fbHeight);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

  VkRect2D scissor = {};
  scissor.offset = {0, 0};
  scissor.extent = {fbWidth, fbHeight};
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

VulkanRenderPass::~VulkanRenderPass() {
}

GPU* VulkanRenderPass::gpu() const {
  return vulkanGPU;
}

void VulkanRenderPass::setViewport(int x, int y, int width, int height) {
  VkViewport viewport = {};
  viewport.x = static_cast<float>(x);
  viewport.y = static_cast<float>(y);
  viewport.width = static_cast<float>(width);
  viewport.height = static_cast<float>(height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
}

void VulkanRenderPass::setScissorRect(int x, int y, int width, int height) {
  VkRect2D scissor = {};
  scissor.offset = {x, y};
  scissor.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void VulkanRenderPass::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
  if (!pipeline) {
    return;
  }
  currentPipeline = std::static_pointer_cast<VulkanRenderPipeline>(pipeline);
  if (currentPipeline->vulkanPipeline() == VK_NULL_HANDLE) {
    currentPipeline = nullptr;
    return;
  }
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    currentPipeline->vulkanPipeline());
  descriptorDirty = true;
}

void VulkanRenderPass::setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer,
                                        size_t offset, size_t size) {
  if (!buffer || binding >= uniformBindings.size()) {
    return;
  }
  auto vulkanBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);
  uniformBindings[binding] = {vulkanBuffer->vulkanBuffer(), offset, size};
  descriptorDirty = true;
}

void VulkanRenderPass::setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                                  std::shared_ptr<Sampler> sampler) {
  if (!texture || !sampler || binding >= textureBindings.size()) {
    return;
  }
  auto vulkanTexture = std::static_pointer_cast<VulkanTexture>(texture);
  auto vulkanSampler = std::static_pointer_cast<VulkanSampler>(sampler);
  textureBindings[binding] = {vulkanTexture->vulkanImageView(), vulkanSampler->vulkanSampler()};
  descriptorDirty = true;
}

void VulkanRenderPass::bindDescriptorSetIfDirty() {
  if (!descriptorDirty || !currentPipeline) {
    return;
  }
  descriptorDirty = false;

  auto device = vulkanGPU->device();
  auto setLayout = currentPipeline->vulkanDescriptorSetLayout();
  if (setLayout == VK_NULL_HANDLE) {
    return;
  }

  auto pool = encoder->vulkanDescriptorPool();
  if (pool == VK_NULL_HANDLE) {
    return;
  }

  VkDescriptorSetAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = pool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &setLayout;

  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  auto result = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
  if (result != VK_SUCCESS) {
    LOGE("VulkanRenderPass: vkAllocateDescriptorSets failed (result=%d). Pool may be exhausted.",
         static_cast<int>(result));
    return;
  }

  std::vector<VkWriteDescriptorSet> writes;
  std::vector<VkDescriptorBufferInfo> bufferInfos;
  std::vector<VkDescriptorImageInfo> imageInfos;
  bufferInfos.reserve(uniformBindings.size());
  imageInfos.reserve(textureBindings.size());

  for (unsigned i = 0; i < static_cast<unsigned>(uniformBindings.size()); i++) {
    auto& ub = uniformBindings[i];
    if (ub.buffer == VK_NULL_HANDLE || !currentPipeline->hasUniformBinding(i)) {
      continue;
    }
    bufferInfos.push_back({ub.buffer, ub.offset, ub.size});
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = i;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfos.back();
    writes.push_back(write);
  }

  for (unsigned i = 0; i < static_cast<unsigned>(textureBindings.size()); i++) {
    auto& tb = textureBindings[i];
    if (tb.imageView == VK_NULL_HANDLE || tb.sampler == VK_NULL_HANDLE ||
        !currentPipeline->hasTextureBinding(i)) {
      continue;
    }
    // All sampled textures use GENERAL layout, consistent with the layout set by writeTexture().
    imageInfos.push_back({tb.sampler, tb.imageView, VK_IMAGE_LAYOUT_GENERAL});
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = i;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfos.back();
    writes.push_back(write);
  }

  if (!writes.empty()) {
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          currentPipeline->vulkanPipelineLayout(), 0, 1, &descriptorSet, 0,
                          nullptr);
}

void VulkanRenderPass::setVertexBuffer(unsigned slot, std::shared_ptr<GPUBuffer> buffer,
                                       size_t offset) {
  if (!buffer) {
    return;
  }
  auto vulkanBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);
  VkBuffer vkBuf = vulkanBuffer->vulkanBuffer();
  VkDeviceSize vkOffset = offset;
  vkCmdBindVertexBuffers(commandBuffer, slot, 1, &vkBuf, &vkOffset);
}

void VulkanRenderPass::setIndexBuffer(std::shared_ptr<GPUBuffer> buffer, IndexFormat format) {
  if (!buffer) {
    return;
  }
  currentIndexBuffer = buffer;
  currentIndexType = (format == IndexFormat::UInt32) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
  auto vulkanBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);
  vkCmdBindIndexBuffer(commandBuffer, vulkanBuffer->vulkanBuffer(), 0, currentIndexType);
}

void VulkanRenderPass::setStencilReference(uint32_t reference) {
  vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
}

void VulkanRenderPass::draw(PrimitiveType primitiveType, uint32_t vertexCount,
                            uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
  if (!currentPipeline) {
    return;
  }
  bindDescriptorSetIfDirty();
  auto vkTopology = primitiveType == PrimitiveType::TriangleStrip
                        ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
                        : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  vkCmdSetPrimitiveTopologyEXT(commandBuffer, vkTopology);
  vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanRenderPass::drawIndexed(PrimitiveType primitiveType, uint32_t indexCount,
                                   uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex,
                                   uint32_t firstInstance) {
  if (!currentPipeline) {
    return;
  }
  bindDescriptorSetIfDirty();
  auto vkTopology = primitiveType == PrimitiveType::TriangleStrip
                        ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
                        : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  vkCmdSetPrimitiveTopologyEXT(commandBuffer, vkTopology);
  vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
}

void VulkanRenderPass::onEnd() {
  vkCmdEndRenderPass(commandBuffer);
  encoder->addDeferredDestroy(renderPass, framebuffer);
  renderPass = VK_NULL_HANDLE;
  framebuffer = VK_NULL_HANDLE;
}

}  // namespace tgfx
