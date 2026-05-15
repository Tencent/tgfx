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

#include "VulkanRenderPipeline.h"
#include "VulkanGPU.h"
#include "VulkanShaderModule.h"
#include "VulkanUtil.h"
#include "core/utils/Log.h"
#include "gpu/UniformData.h"

namespace tgfx {

static VkBlendFactor ToVkBlendFactor(BlendFactor factor) {
  switch (factor) {
    case BlendFactor::Zero:
      return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One:
      return VK_BLEND_FACTOR_ONE;
    case BlendFactor::Src:
      return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::OneMinusSrc:
      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::Dst:
      return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::OneMinusDst:
      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::SrcAlpha:
      return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha:
      return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::OneMinusDstAlpha:
      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    default:
      return VK_BLEND_FACTOR_ZERO;
  }
}

static VkBlendOp ToVkBlendOp(BlendOperation op) {
  switch (op) {
    case BlendOperation::Add:
      return VK_BLEND_OP_ADD;
    case BlendOperation::Subtract:
      return VK_BLEND_OP_SUBTRACT;
    case BlendOperation::ReverseSubtract:
      return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOperation::Min:
      return VK_BLEND_OP_MIN;
    case BlendOperation::Max:
      return VK_BLEND_OP_MAX;
    default:
      return VK_BLEND_OP_ADD;
  }
}

static VkCompareOp ToVkCompareOp(CompareFunction func) {
  switch (func) {
    case CompareFunction::Never:
      return VK_COMPARE_OP_NEVER;
    case CompareFunction::Less:
      return VK_COMPARE_OP_LESS;
    case CompareFunction::Equal:
      return VK_COMPARE_OP_EQUAL;
    case CompareFunction::LessEqual:
      return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareFunction::Greater:
      return VK_COMPARE_OP_GREATER;
    case CompareFunction::NotEqual:
      return VK_COMPARE_OP_NOT_EQUAL;
    case CompareFunction::GreaterEqual:
      return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareFunction::Always:
      return VK_COMPARE_OP_ALWAYS;
    default:
      return VK_COMPARE_OP_ALWAYS;
  }
}

static VkStencilOp ToVkStencilOp(StencilOperation op) {
  switch (op) {
    case StencilOperation::Keep:
      return VK_STENCIL_OP_KEEP;
    case StencilOperation::Zero:
      return VK_STENCIL_OP_ZERO;
    case StencilOperation::Replace:
      return VK_STENCIL_OP_REPLACE;
    case StencilOperation::IncrementClamp:
      return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case StencilOperation::DecrementClamp:
      return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case StencilOperation::Invert:
      return VK_STENCIL_OP_INVERT;
    case StencilOperation::IncrementWrap:
      return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case StencilOperation::DecrementWrap:
      return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default:
      return VK_STENCIL_OP_KEEP;
  }
}

static VkFormat AttributeToVkFormat(const Attribute& attr) {
  switch (attr.format()) {
    case VertexFormat::Float:
      return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2:
      return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3:
      return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::UByte4Normalized:
      return VK_FORMAT_R8G8B8A8_UNORM;
    default:
      return VK_FORMAT_UNDEFINED;
  }
}

std::shared_ptr<VulkanRenderPipeline> VulkanRenderPipeline::Make(
    VulkanGPU* gpu, const RenderPipelineDescriptor& descriptor) {
  if (!gpu) {
    return nullptr;
  }
  auto pipeline = gpu->makeResource<VulkanRenderPipeline>(gpu, descriptor);
  if (pipeline->pipeline == VK_NULL_HANDLE) {
    return nullptr;
  }
  return pipeline;
}

VulkanRenderPipeline::VulkanRenderPipeline(VulkanGPU* gpu,
                                           const RenderPipelineDescriptor& descriptor) {
  if (!createDescriptorSetLayout(gpu, descriptor)) {
    return;
  }
  if (!createPipelineLayout(gpu)) {
    return;
  }
  if (!createPipeline(gpu, descriptor, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, &pipeline)) {
    return;
  }
  // When extendedDynamicState is unavailable, create a TriangleStrip pipeline variant so that
  // draw calls requesting strip topology can bind the correct pipeline instead of falling back
  // to an incorrect TriangleList interpretation.
  if (!gpu->extensions().extendedDynamicState) {
    if (!createPipeline(gpu, descriptor, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, &stripPipeline)) {
      vkDestroyPipeline(gpu->device(), pipeline, nullptr);
      pipeline = VK_NULL_HANDLE;
      return;
    }
  }

  unsigned textureUnit = 0;
  for (auto& entry : descriptor.layout.textureSamplers) {
    auto spirvBinding = static_cast<unsigned>(TEXTURE_BINDING_POINT_START) + textureUnit;
    textureUnits[entry.binding] = textureUnit++;
    textureDescriptorBindings[entry.binding] = spirvBinding;
    textureBindingSet.insert(entry.binding);
  }
  for (auto& entry : descriptor.layout.uniformBlocks) {
    uniformBlockVisibility[entry.binding] = entry.visibility;
    uniformBindingSet.insert(entry.binding);
  }

  // Verify uniform and texture descriptor bindings do not overlap in the same descriptor set.
  for (auto& [userBinding, descriptorBinding] : textureDescriptorBindings) {
    DEBUG_ASSERT(uniformBindingSet.count(descriptorBinding) == 0);
  }
}

void VulkanRenderPipeline::onRelease(VulkanGPU* gpu) {
  auto device = gpu->device();
  if (pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
  }
  if (stripPipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, stripPipeline, nullptr);
    stripPipeline = VK_NULL_HANDLE;
  }
  if (pipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    pipelineLayout = VK_NULL_HANDLE;
  }
  if (descriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    descriptorSetLayout = VK_NULL_HANDLE;
  }
}

unsigned VulkanRenderPipeline::getTextureIndex(unsigned binding) const {
  auto result = textureUnits.find(binding);
  if (result != textureUnits.end()) {
    return result->second;
  }
  return binding;
}

unsigned VulkanRenderPipeline::getDescriptorBinding(unsigned binding) const {
  auto result = textureDescriptorBindings.find(binding);
  if (result != textureDescriptorBindings.end()) {
    return result->second;
  }
  return binding;
}

uint32_t VulkanRenderPipeline::getUniformBlockVisibility(unsigned binding) const {
  auto result = uniformBlockVisibility.find(binding);
  if (result != uniformBlockVisibility.end()) {
    return result->second;
  }
  return ShaderVisibility::VertexFragment;
}

bool VulkanRenderPipeline::createDescriptorSetLayout(VulkanGPU* gpu,
                                                     const RenderPipelineDescriptor& descriptor) {
  std::vector<VkDescriptorSetLayoutBinding> bindings;

  for (auto& entry : descriptor.layout.uniformBlocks) {
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = entry.binding;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back(binding);
  }

  auto samplerCount = descriptor.layout.textureSamplers.size();
  for (size_t i = 0; i < samplerCount; ++i) {
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = static_cast<unsigned>(TEXTURE_BINDING_POINT_START) + static_cast<unsigned>(i);
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back(binding);
  }

  VkDescriptorSetLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  auto result =
      vkCreateDescriptorSetLayout(gpu->device(), &layoutInfo, nullptr, &descriptorSetLayout);
  if (result != VK_SUCCESS) {
    LOGE("VulkanRenderPipeline: vkCreateDescriptorSetLayout failed.");
    return false;
  }
  return true;
}

bool VulkanRenderPipeline::createPipelineLayout(VulkanGPU* gpu) {
  VkPipelineLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &descriptorSetLayout;

  auto result = vkCreatePipelineLayout(gpu->device(), &layoutInfo, nullptr, &pipelineLayout);
  if (result != VK_SUCCESS) {
    LOGE("VulkanRenderPipeline: vkCreatePipelineLayout failed.");
    return false;
  }
  return true;
}

bool VulkanRenderPipeline::createPipeline(VulkanGPU* gpu,
                                          const RenderPipelineDescriptor& descriptor,
                                          VkPrimitiveTopology topology, VkPipeline* outPipeline) {
  // Shader stages
  std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

  if (descriptor.vertex.module) {
    auto vertexShader = std::static_pointer_cast<VulkanShaderModule>(descriptor.vertex.module);
    if (vertexShader->vulkanShaderModule() == VK_NULL_HANDLE) {
      LOGE("VulkanRenderPipeline: vertex shader module is invalid.");
      return false;
    }
    VkPipelineShaderStageCreateInfo stage = {};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage.module = vertexShader->vulkanShaderModule();
    stage.pName = "main";
    shaderStages.push_back(stage);
  }

  if (descriptor.fragment.module) {
    auto fragmentShader = std::static_pointer_cast<VulkanShaderModule>(descriptor.fragment.module);
    if (fragmentShader->vulkanShaderModule() == VK_NULL_HANDLE) {
      LOGE("VulkanRenderPipeline: fragment shader module is invalid.");
      return false;
    }
    VkPipelineShaderStageCreateInfo stage = {};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stage.module = fragmentShader->vulkanShaderModule();
    stage.pName = "main";
    shaderStages.push_back(stage);
  }

  // Vertex input
  std::vector<VkVertexInputBindingDescription> vertexBindings;
  std::vector<VkVertexInputAttributeDescription> vertexAttributes;

  uint32_t globalLocation = 0;
  for (uint32_t i = 0; i < static_cast<uint32_t>(descriptor.vertex.bufferLayouts.size()); i++) {
    const auto& layout = descriptor.vertex.bufferLayouts[i];
    VkVertexInputBindingDescription binding = {};
    binding.binding = i;
    binding.stride = static_cast<uint32_t>(layout.stride);
    binding.inputRate = (layout.stepMode == VertexStepMode::Instance)
                            ? VK_VERTEX_INPUT_RATE_INSTANCE
                            : VK_VERTEX_INPUT_RATE_VERTEX;
    vertexBindings.push_back(binding);

    uint32_t offset = 0;
    for (const auto& attr : layout.attributes) {
      VkVertexInputAttributeDescription attrDesc = {};
      attrDesc.location = globalLocation++;
      attrDesc.binding = i;
      attrDesc.format = AttributeToVkFormat(attr);
      attrDesc.offset = offset;
      offset += static_cast<uint32_t>(attr.size());
      vertexAttributes.push_back(attrDesc);
    }
  }

  VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.size());
  vertexInputInfo.pVertexBindingDescriptions = vertexBindings.data();
  vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
  vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = topology;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  // Dynamic viewport and scissor
  VkPipelineViewportStateCreateInfo viewportState = {};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer = {};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  if (descriptor.primitive.cullMode == CullMode::Front) {
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
  } else if (descriptor.primitive.cullMode == CullMode::Back) {
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  } else {
    rasterizer.cullMode = VK_CULL_MODE_NONE;
  }
  rasterizer.frontFace = (descriptor.primitive.frontFace == FrontFace::CW)
                             ? VK_FRONT_FACE_CLOCKWISE
                             : VK_FRONT_FACE_COUNTER_CLOCKWISE;

  VkPipelineMultisampleStateCreateInfo multisampling = {};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.rasterizationSamples =
      static_cast<VkSampleCountFlagBits>(descriptor.multisample.count);
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.alphaToCoverageEnable =
      descriptor.multisample.alphaToCoverageEnabled ? VK_TRUE : VK_FALSE;
  VkSampleMask sampleMask = descriptor.multisample.mask;
  if (sampleMask != 0xFFFFFFFF) {
    multisampling.pSampleMask = &sampleMask;
  }

  // Color blend attachments
  std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
  for (auto& ca : descriptor.fragment.colorAttachments) {
    VkPipelineColorBlendAttachmentState attachment = {};
    attachment.blendEnable = ca.blendEnable ? VK_TRUE : VK_FALSE;
    attachment.srcColorBlendFactor = ToVkBlendFactor(ca.srcColorBlendFactor);
    attachment.dstColorBlendFactor = ToVkBlendFactor(ca.dstColorBlendFactor);
    attachment.colorBlendOp = ToVkBlendOp(ca.colorBlendOp);
    attachment.srcAlphaBlendFactor = ToVkBlendFactor(ca.srcAlphaBlendFactor);
    attachment.dstAlphaBlendFactor = ToVkBlendFactor(ca.dstAlphaBlendFactor);
    attachment.alphaBlendOp = ToVkBlendOp(ca.alphaBlendOp);
    attachment.colorWriteMask = 0;
    if (ca.colorWriteMask & ColorWriteMask::RED)
      attachment.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
    if (ca.colorWriteMask & ColorWriteMask::GREEN)
      attachment.colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
    if (ca.colorWriteMask & ColorWriteMask::BLUE)
      attachment.colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
    if (ca.colorWriteMask & ColorWriteMask::ALPHA)
      attachment.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachments.push_back(attachment);
  }

  VkPipelineColorBlendStateCreateInfo colorBlending = {};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
  colorBlending.pAttachments = colorBlendAttachments.data();

  // Depth stencil
  VkPipelineDepthStencilStateCreateInfo depthStencil = {};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable = (descriptor.depthStencil.depthCompare != CompareFunction::Always ||
                                  descriptor.depthStencil.depthWriteEnabled)
                                     ? VK_TRUE
                                     : VK_FALSE;
  depthStencil.depthWriteEnable = descriptor.depthStencil.depthWriteEnabled ? VK_TRUE : VK_FALSE;
  depthStencil.depthCompareOp = ToVkCompareOp(descriptor.depthStencil.depthCompare);
  depthStencil.stencilTestEnable =
      (descriptor.depthStencil.stencilFront.compare != CompareFunction::Always ||
       descriptor.depthStencil.stencilBack.compare != CompareFunction::Always ||
       descriptor.depthStencil.stencilFront.failOp != StencilOperation::Keep ||
       descriptor.depthStencil.stencilFront.passOp != StencilOperation::Keep ||
       descriptor.depthStencil.stencilFront.depthFailOp != StencilOperation::Keep ||
       descriptor.depthStencil.stencilBack.failOp != StencilOperation::Keep ||
       descriptor.depthStencil.stencilBack.passOp != StencilOperation::Keep ||
       descriptor.depthStencil.stencilBack.depthFailOp != StencilOperation::Keep)
          ? VK_TRUE
          : VK_FALSE;
  depthStencil.front.compareOp = ToVkCompareOp(descriptor.depthStencil.stencilFront.compare);
  depthStencil.front.failOp = ToVkStencilOp(descriptor.depthStencil.stencilFront.failOp);
  depthStencil.front.depthFailOp = ToVkStencilOp(descriptor.depthStencil.stencilFront.depthFailOp);
  depthStencil.front.passOp = ToVkStencilOp(descriptor.depthStencil.stencilFront.passOp);
  depthStencil.front.compareMask = descriptor.depthStencil.stencilReadMask;
  depthStencil.front.writeMask = descriptor.depthStencil.stencilWriteMask;
  depthStencil.back.compareOp = ToVkCompareOp(descriptor.depthStencil.stencilBack.compare);
  depthStencil.back.failOp = ToVkStencilOp(descriptor.depthStencil.stencilBack.failOp);
  depthStencil.back.depthFailOp = ToVkStencilOp(descriptor.depthStencil.stencilBack.depthFailOp);
  depthStencil.back.passOp = ToVkStencilOp(descriptor.depthStencil.stencilBack.passOp);
  depthStencil.back.compareMask = descriptor.depthStencil.stencilReadMask;
  depthStencil.back.writeMask = descriptor.depthStencil.stencilWriteMask;

  // Dynamic states
  std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                               VK_DYNAMIC_STATE_STENCIL_REFERENCE};
  if (gpu->extensions().extendedDynamicState) {
    dynamicStates.push_back(VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT);
  }
  VkPipelineDynamicStateCreateInfo dynamicState = {};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  // Render pass (dynamic rendering not used; create a compatible render pass)
  // For now we need color attachment formats and depth format.
  std::vector<VkAttachmentDescription> attachments;
  std::vector<VkAttachmentReference> colorRefs;
  std::vector<VkAttachmentReference> resolveRefs;
  // OpsRenderTask always sets resolveTexture when sampleCount > 1 (via getSampleTexture()), so
  // the pipeline render pass layout matches the runtime VulkanRenderPass layout. If MSAA without
  // resolve is ever needed, this must be driven by a per-color-attachment flag instead.
  bool hasResolve = (descriptor.multisample.count > 1);

  for (auto& ca : descriptor.fragment.colorAttachments) {
    VkAttachmentDescription attachment = {};
    attachment.format = gpu->getVkFormat(ca.format);
    attachment.samples = static_cast<VkSampleCountFlagBits>(descriptor.multisample.count);
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    colorRefs.push_back(
        {static_cast<uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    attachments.push_back(attachment);

    // When MSAA is enabled, assume a resolve attachment per color attachment to match the runtime
    // VulkanRenderPass layout (which adds one whenever resolveTexture is set).
    if (hasResolve) {
      VkAttachmentDescription resolveAttachment = {};
      resolveAttachment.format = attachment.format;
      resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
      resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
      resolveRefs.push_back(
          {static_cast<uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
      attachments.push_back(resolveAttachment);
    }
  }

  VkAttachmentReference depthRef = {};
  bool hasDepth = (descriptor.depthStencil.format != PixelFormat::Unknown);
  if (hasDepth) {
    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = gpu->getVkFormat(descriptor.depthStencil.format);
    depthAttachment.samples = static_cast<VkSampleCountFlagBits>(descriptor.multisample.count);
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthRef = {static_cast<uint32_t>(attachments.size()),
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    attachments.push_back(depthAttachment);
  }

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
  subpass.pColorAttachments = colorRefs.data();
  subpass.pResolveAttachments = hasResolve ? resolveRefs.data() : nullptr;
  subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

  // Subpass dependencies and attachment layouts must match VulkanRenderPass to ensure render pass
  // compatibility (Vulkan spec §8.2). See VulkanRenderPass constructor for detailed rationale.
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

  VkRenderPassCreateInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 2;
  renderPassInfo.pDependencies = dependencies;

  VkRenderPass renderPass = VK_NULL_HANDLE;
  auto result = vkCreateRenderPass(gpu->device(), &renderPassInfo, nullptr, &renderPass);
  if (result != VK_SUCCESS) {
    LOGE("VulkanRenderPipeline: vkCreateRenderPass failed.");
    return false;
  }

  // Create graphics pipeline
  VkGraphicsPipelineCreateInfo pipelineInfo = {};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
  pipelineInfo.pStages = shaderStages.data();
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = pipelineLayout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  result = vkCreateGraphicsPipelines(gpu->device(), gpu->pipelineCache(), 1, &pipelineInfo, nullptr,
                                     outPipeline);

  // The render pass object used for pipeline creation can be destroyed immediately since Vulkan
  // only requires render pass compatibility (not identity) at draw time.
  vkDestroyRenderPass(gpu->device(), renderPass, nullptr);

  if (result != VK_SUCCESS) {
    LOGE("VulkanRenderPipeline: vkCreateGraphicsPipelines failed.");
    return false;
  }
  return true;
}

}  // namespace tgfx
