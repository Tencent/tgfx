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
  auto vulkanGPU = static_cast<VulkanGPU*>(encoder->gpu());
  auto pass =
      std::shared_ptr<VulkanRenderPass>(new VulkanRenderPass(encoder, vulkanGPU, descriptor));
  if (pass->renderPass == VK_NULL_HANDLE) {
    return nullptr;
  }
  return pass;
}

VulkanRenderPass::VulkanRenderPass(VulkanCommandEncoder* encoder, VulkanGPU* gpu,
                                   const RenderPassDescriptor& passDescriptor)
    : RenderPass(passDescriptor), encoder(encoder), vulkanGPU(gpu),
      commandBuffer(encoder->vulkanCommandBuffer()) {
  // Every resource bound during this render pass is retained via encoder->retainResource().
  // This ensures the resource's shared_ptr refcount stays >0 until GPU execution completes,
  // even if the application releases its own reference before the fence signals.

  auto device = vulkanGPU->device();

  lastBound.uniformBindings.resize(16);
  lastBound.textureBindings.resize(16);
  lastBound.vertexBindings.resize(4);

  std::vector<VkAttachmentDescription> attachments;
  std::vector<VkAttachmentReference> colorRefs;
  std::vector<VkAttachmentReference> resolveRefs;
  std::vector<VkImageView> fbAttachments;
  uint32_t fbWidth = 0;
  uint32_t fbHeight = 0;
  std::vector<VkClearValue> clearValues;
  bool hasResolve = false;

  for (auto& ca : passDescriptor.colorAttachments) {
    if (!ca.texture) {
      continue;
    }
    auto vulkanTexture = std::static_pointer_cast<VulkanTexture>(ca.texture);
    encoder->retainResource(vulkanTexture);

    // Transition color attachment to GENERAL layout. For Clear/DontCare loadOp, using UNDEFINED as
    // oldLayout avoids cross-submission layout tracking issues. For Load, we must preserve contents
    // so we use the tracked layout (the previous submit that wrote this image will have completed
    // on the same queue before this command buffer executes).
    auto loadAction = ca.loadAction;
    if (loadAction == LoadAction::Load &&
        vulkanTexture->currentLayout() == VK_IMAGE_LAYOUT_UNDEFINED) {
      loadAction = LoadAction::DontCare;
    }
    auto oldLayout = (loadAction == LoadAction::Load) ? vulkanTexture->currentLayout()
                                                      : VK_IMAGE_LAYOUT_UNDEFINED;
    TransitionImageLayout(commandBuffer, vulkanTexture->vulkanImage(), oldLayout,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    VkAttachmentDescription attachment = {};
    attachment.format = vulkanTexture->vulkanFormat();
    attachment.samples = static_cast<VkSampleCountFlagBits>(ca.texture->sampleCount());
    attachment.loadOp = ToVkLoadOp(loadAction);
    attachment.storeOp = ToVkStoreOp(ca.storeAction);
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

    vulkanTexture->setCurrentLayout(VK_IMAGE_LAYOUT_GENERAL);

    colorRefs.push_back(
        {static_cast<uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    attachments.push_back(attachment);
    fbAttachments.push_back(vulkanTexture->vulkanImageView());

    VkClearValue clearValue = {};
    clearValue.color = {
        {ca.clearValue.red, ca.clearValue.green, ca.clearValue.blue, ca.clearValue.alpha}};
    clearValues.push_back(clearValue);

    // Configure MSAA resolve attachment if present.
    if (ca.resolveTexture) {
      DEBUG_ASSERT(ca.resolveTexture->sampleCount() == 1);
      if (ca.resolveTexture->sampleCount() != 1) {
        LOGE("VulkanRenderPass: resolve texture must have sampleCount == 1.");
        return;
      }
      hasResolve = true;
      auto resolveVulkanTexture = std::static_pointer_cast<VulkanTexture>(ca.resolveTexture);
      encoder->retainResource(resolveVulkanTexture);

      TransitionImageLayout(commandBuffer, resolveVulkanTexture->vulkanImage(),
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT);

      VkAttachmentDescription resolveAttachment = {};
      resolveAttachment.format = resolveVulkanTexture->vulkanFormat();
      resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
      resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

      resolveVulkanTexture->setCurrentLayout(VK_IMAGE_LAYOUT_GENERAL);

      resolveRefs.push_back(
          {static_cast<uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
      attachments.push_back(resolveAttachment);
      fbAttachments.push_back(resolveVulkanTexture->vulkanImageView());
      clearValues.push_back({});
    } else {
      resolveRefs.push_back({VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED});
    }

    fbWidth = static_cast<uint32_t>(ca.texture->width());
    fbHeight = static_cast<uint32_t>(ca.texture->height());
  }

  VkAttachmentReference depthRef = {};
  bool hasDepth = (passDescriptor.depthStencilAttachment.texture != nullptr);
  if (hasDepth) {
    auto dsTexture =
        std::static_pointer_cast<VulkanTexture>(passDescriptor.depthStencilAttachment.texture);
    encoder->retainResource(dsTexture);

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
  subpass.pResolveAttachments = hasResolve ? resolveRefs.data() : nullptr;
  subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

  // Explicit subpass dependencies replace the implicit default (TOP_OF_PIPE↔ALL_COMMANDS).
  // EXTERNAL→0: ensures prior color attachment writes (from a previous render pass) or transfers
  // are visible before this subpass starts writing.
  // 0→EXTERNAL: ensures this subpass's color attachment writes are visible to subsequent operations
  // (shader reads, transfers, or presentation layout transitions).
  VkSubpassDependency dependencies[2] = {};
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[0].dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask =
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

  VkRenderPassCreateInfo rpInfo = {};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  rpInfo.pAttachments = attachments.data();
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  rpInfo.dependencyCount = 2;
  rpInfo.pDependencies = dependencies;

  auto rpResult = vkCreateRenderPass(device, &rpInfo, nullptr, &renderPass);
  if (rpResult != VK_SUCCESS) {
    LOGE("VulkanRenderPass: vkCreateRenderPass failed: %s", VkResultToString(rpResult));
    return;
  }

  VkFramebufferCreateInfo fbInfo = {};
  fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fbInfo.renderPass = renderPass;
  fbInfo.attachmentCount = static_cast<uint32_t>(fbAttachments.size());
  fbInfo.pAttachments = fbAttachments.data();
  fbInfo.width = fbWidth;
  fbInfo.height = fbHeight;
  fbInfo.layers = 1;

  auto fbResult = vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffer);
  if (fbResult != VK_SUCCESS) {
    LOGE("VulkanRenderPass: vkCreateFramebuffer failed: %s", VkResultToString(fbResult));
    vkDestroyRenderPass(device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;
    return;
  }

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
  if (x == lastBound.scissorX && y == lastBound.scissorY && width == lastBound.scissorWidth &&
      height == lastBound.scissorHeight) {
    return;
  }
  lastBound.scissorX = x;
  lastBound.scissorY = y;
  lastBound.scissorWidth = width;
  lastBound.scissorHeight = height;
  VkRect2D scissor = {};
  scissor.offset = {x, y};
  scissor.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void VulkanRenderPass::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
  if (!pipeline) {
    return;
  }
  auto vulkanPipeline = std::static_pointer_cast<VulkanRenderPipeline>(pipeline);
  if (vulkanPipeline == lastBound.pipeline) {
    return;
  }
  lastBound.pipeline = vulkanPipeline;
  if (lastBound.pipeline->vulkanPipeline() == VK_NULL_HANDLE) {
    lastBound.pipeline = nullptr;
    return;
  }
  encoder->retainResource(std::static_pointer_cast<VulkanResource>(lastBound.pipeline));
  auto vkPipeline = lastBound.pipeline->vulkanPipeline();
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
  lastBound.boundVkPipeline = vkPipeline;
  lastBound.descriptorDirty = true;
}

void VulkanRenderPass::setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer,
                                        size_t offset, size_t size) {
  if (!buffer || binding >= lastBound.uniformBindings.size()) {
    return;
  }
  auto vulkanBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);
  auto vkBuf = vulkanBuffer->vulkanBuffer();
  auto& ub = lastBound.uniformBindings[binding];
  if (ub.buffer == vkBuf && ub.offset == offset && ub.size == size) {
    return;
  }
  encoder->retainResource(vulkanBuffer);
  ub = {vkBuf, offset, size};
  lastBound.descriptorDirty = true;
}

void VulkanRenderPass::setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                                  std::shared_ptr<Sampler> sampler) {
  if (!texture || !sampler || !lastBound.pipeline) {
    return;
  }
  auto unitIndex = lastBound.pipeline->getTextureIndex(binding);
  if (unitIndex >= lastBound.textureBindings.size()) {
    return;
  }
  auto vulkanTexture = std::static_pointer_cast<VulkanTexture>(texture);
  auto vulkanSampler = std::static_pointer_cast<VulkanSampler>(sampler);
  auto view = vulkanTexture->vulkanImageView();
  auto samp = vulkanSampler->vulkanSampler();
  auto& tb = lastBound.textureBindings[unitIndex];
  if (tb.imageView == view && tb.sampler == samp) {
    return;
  }
  encoder->retainResource(vulkanTexture);
  encoder->retainResource(vulkanSampler);
  tb = {view, samp};
  lastBound.descriptorDirty = true;
}

void VulkanRenderPass::bindDescriptorSetIfDirty() {
  if (!lastBound.descriptorDirty || !lastBound.pipeline) {
    return;
  }
  lastBound.descriptorDirty = false;

  auto device = vulkanGPU->device();
  auto setLayout = lastBound.pipeline->vulkanDescriptorSetLayout();
  if (setLayout == VK_NULL_HANDLE) {
    return;
  }

  auto descriptorSet = encoder->allocateDescriptorSet(setLayout);
  if (descriptorSet == VK_NULL_HANDLE) {
    LOGE("VulkanRenderPass: descriptor set allocation failed, draw will be dropped.");
    return;
  }

  std::vector<VkWriteDescriptorSet> writes;
  std::vector<VkDescriptorBufferInfo> bufferInfos;
  std::vector<VkDescriptorImageInfo> imageInfos;
  bufferInfos.reserve(lastBound.uniformBindings.size());
  imageInfos.reserve(lastBound.textureBindings.size());

  for (unsigned i = 0; i < static_cast<unsigned>(lastBound.uniformBindings.size()); i++) {
    auto& ub = lastBound.uniformBindings[i];
    if (ub.buffer == VK_NULL_HANDLE || !lastBound.pipeline->hasUniformBinding(i)) {
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

  for (auto userBinding : lastBound.pipeline->getTextureBindings()) {
    auto unitIndex = lastBound.pipeline->getTextureIndex(userBinding);
    if (unitIndex >= lastBound.textureBindings.size()) {
      continue;
    }
    auto& tb = lastBound.textureBindings[unitIndex];
    if (tb.imageView == VK_NULL_HANDLE || tb.sampler == VK_NULL_HANDLE) {
      continue;
    }
    // All sampled textures use GENERAL layout, consistent with the layout set by writeTexture().
    imageInfos.push_back({tb.sampler, tb.imageView, VK_IMAGE_LAYOUT_GENERAL});
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = lastBound.pipeline->getDescriptorBinding(userBinding);
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfos.back();
    writes.push_back(write);
  }

  if (!writes.empty()) {
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
  }

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          lastBound.pipeline->vulkanPipelineLayout(), 0, 1, &descriptorSet, 0,
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
  if (slot < lastBound.vertexBindings.size()) {
    auto& last = lastBound.vertexBindings[slot];
    if (last.buffer == vkBuf && last.offset == vkOffset) {
      return;
    }
    last = {vkBuf, vkOffset};
  }
  encoder->retainResource(vulkanBuffer);
  vkCmdBindVertexBuffers(commandBuffer, slot, 1, &vkBuf, &vkOffset);
}

void VulkanRenderPass::setIndexBuffer(std::shared_ptr<GPUBuffer> buffer, IndexFormat format) {
  if (!buffer) {
    return;
  }
  auto vulkanBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);
  auto vkBuf = vulkanBuffer->vulkanBuffer();
  auto indexType = (format == IndexFormat::UInt32) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
  if (vkBuf == lastBound.indexBuffer && indexType == lastBound.indexType) {
    return;
  }
  lastBound.indexBuffer = vkBuf;
  lastBound.indexType = indexType;
  encoder->retainResource(vulkanBuffer);
  vkCmdBindIndexBuffer(commandBuffer, vkBuf, 0, indexType);
}

void VulkanRenderPass::setStencilReference(uint32_t reference) {
  vkCmdSetStencilReference(commandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
}

void VulkanRenderPass::draw(PrimitiveType primitiveType, uint32_t vertexCount,
                            uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
  if (!lastBound.pipeline) {
    return;
  }
  bindDescriptorSetIfDirty();
  if (vulkanGPU->extensions().extendedDynamicState) {
    auto vkTopology = primitiveType == PrimitiveType::TriangleStrip
                          ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
                          : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    vkCmdSetPrimitiveTopologyEXT(commandBuffer, vkTopology);
  } else {
    // Bind the pipeline variant matching the requested topology. The VulkanRenderPipeline holds
    // both a TriangleList and a TriangleStrip VkPipeline when extendedDynamicState is unavailable.
    auto targetPipeline = lastBound.pipeline->vulkanPipeline(primitiveType);
    if (targetPipeline != lastBound.boundVkPipeline) {
      vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, targetPipeline);
      lastBound.boundVkPipeline = targetPipeline;
    }
  }
  vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanRenderPass::drawIndexed(PrimitiveType primitiveType, uint32_t indexCount,
                                   uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex,
                                   uint32_t firstInstance) {
  if (!lastBound.pipeline) {
    return;
  }
  bindDescriptorSetIfDirty();
  if (vulkanGPU->extensions().extendedDynamicState) {
    auto vkTopology = primitiveType == PrimitiveType::TriangleStrip
                          ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
                          : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    vkCmdSetPrimitiveTopologyEXT(commandBuffer, vkTopology);
  } else {
    auto targetPipeline = lastBound.pipeline->vulkanPipeline(primitiveType);
    if (targetPipeline != lastBound.boundVkPipeline) {
      vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, targetPipeline);
      lastBound.boundVkPipeline = targetPipeline;
    }
  }
  vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
}

void VulkanRenderPass::onEnd() {
  vkCmdEndRenderPass(commandBuffer);
  encoder->addDeferredDestroy(renderPass, framebuffer);
  renderPass = VK_NULL_HANDLE;
  framebuffer = VK_NULL_HANDLE;
}

}  // namespace tgfx
