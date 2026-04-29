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

std::shared_ptr<VulkanRenderPass> VulkanRenderPass::Make(VulkanCommandEncoder* encoder,
                                                         const RenderPassDescriptor& descriptor) {
  if (!encoder) {
    return nullptr;
  }
  return std::shared_ptr<VulkanRenderPass>(new VulkanRenderPass(encoder, descriptor));
}

VulkanRenderPass::VulkanRenderPass(VulkanCommandEncoder* encoder,
                                   const RenderPassDescriptor& passDescriptor)
    : RenderPass(passDescriptor), encoder(encoder),
      commandBuffer(encoder->vulkanCommandBuffer()) {

  auto vulkanGPU = static_cast<VulkanGPU*>(encoder->gpu());
  auto device = vulkanGPU->device();

  // Build render pass and framebuffer from the descriptor.
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
    VkAttachmentDescription attachment = {};
    attachment.format = vulkanTexture->vulkanFormat();
    attachment.samples = static_cast<VkSampleCountFlagBits>(ca.texture->sampleCount());
    attachment.loadOp = ToVkLoadOp(ca.loadAction);
    attachment.storeOp = ToVkStoreOp(ca.storeAction);
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    colorRefs.push_back(
        {static_cast<uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    attachments.push_back(attachment);
    fbAttachments.push_back(vulkanTexture->vulkanImageView());

    VkClearValue clearValue = {};
    clearValue.color = {{ca.clearValue.red, ca.clearValue.green, ca.clearValue.blue,
                         ca.clearValue.alpha}};
    clearValues.push_back(clearValue);

    fbWidth = static_cast<uint32_t>(ca.texture->width());
    fbHeight = static_cast<uint32_t>(ca.texture->height());
  }

  VkAttachmentReference depthRef = {};
  bool hasDepth = (passDescriptor.depthStencilAttachment.texture != nullptr);
  if (hasDepth) {
    auto dsTexture = std::static_pointer_cast<VulkanTexture>(
        passDescriptor.depthStencilAttachment.texture);
    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = dsTexture->vulkanFormat();
    depthAttachment.samples = static_cast<VkSampleCountFlagBits>(dsTexture->sampleCount());
    depthAttachment.loadOp = ToVkLoadOp(passDescriptor.depthStencilAttachment.loadAction);
    depthAttachment.storeOp = ToVkStoreOp(passDescriptor.depthStencilAttachment.storeAction);
    depthAttachment.stencilLoadOp = ToVkLoadOp(passDescriptor.depthStencilAttachment.loadAction);
    depthAttachment.stencilStoreOp = ToVkStoreOp(passDescriptor.depthStencilAttachment.storeAction);
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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
}

VulkanRenderPass::~VulkanRenderPass() {
}

GPU* VulkanRenderPass::gpu() const {
  return encoder->gpu();
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
  currentPipeline = std::static_pointer_cast<VulkanRenderPipeline>(pipeline);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    currentPipeline->vulkanPipeline());
}

void VulkanRenderPass::setUniformBuffer(unsigned, std::shared_ptr<GPUBuffer>, size_t, size_t) {
  // TODO: Implement descriptor set update and binding for uniform buffers.
}

void VulkanRenderPass::setTexture(unsigned, std::shared_ptr<Texture>, std::shared_ptr<Sampler>) {
  // TODO: Implement descriptor set update and binding for combined image samplers.
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

void VulkanRenderPass::draw(PrimitiveType, uint32_t vertexCount, uint32_t instanceCount,
                            uint32_t firstVertex, uint32_t firstInstance) {
  vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanRenderPass::drawIndexed(PrimitiveType, uint32_t indexCount, uint32_t instanceCount,
                                   uint32_t firstIndex, int32_t baseVertex,
                                   uint32_t firstInstance) {
  vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
}

void VulkanRenderPass::onEnd() {
  vkCmdEndRenderPass(commandBuffer);

  auto vulkanGPU = static_cast<VulkanGPU*>(encoder->gpu());
  auto device = vulkanGPU->device();
  if (framebuffer != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    framebuffer = VK_NULL_HANDLE;
  }
  if (renderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;
  }
}

}  // namespace tgfx
