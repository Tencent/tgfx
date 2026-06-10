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

#include "WebGPURenderPipeline.h"
#include "WebGPUDefines.h"
#include "WebGPUGPU.h"
#include "WebGPUShaderModule.h"
#include "WebGPUUtil.h"
#include "core/utils/Log.h"
#include "gpu/UniformData.h"
#include "tgfx/gpu/ShaderVisibility.h"

namespace tgfx {

static void OnPipelineErrorScope(WGPUErrorType type, const char* message, void*) {
  if (type != WGPUErrorType_NoError) {
    LOGE("[WebGPU Pipeline] Validation error: %s", message);
  }
}

std::shared_ptr<WebGPURenderPipeline> WebGPURenderPipeline::Make(
    WebGPUGPU* gpu, const RenderPipelineDescriptor& descriptor) {
  if (gpu == nullptr) {
    return nullptr;
  }
  auto renderPipeline = gpu->makeResource<WebGPURenderPipeline>(gpu, descriptor);
  if (renderPipeline->pipeline == nullptr) {
    return nullptr;
  }
  return renderPipeline;
}

WebGPURenderPipeline::WebGPURenderPipeline(WebGPUGPU* gpu,
                                           const RenderPipelineDescriptor& descriptor) {
  if (!createPipelineState(gpu, descriptor)) {
    LOGE("WebGPU: Failed to create render pipeline state");
  }
}

bool WebGPURenderPipeline::createPipelineState(WebGPUGPU* gpu,
                                               const RenderPipelineDescriptor& descriptor) {
  auto vertexModule = std::static_pointer_cast<WebGPUShaderModule>(descriptor.vertex.module);
  auto fragmentModule = std::static_pointer_cast<WebGPUShaderModule>(descriptor.fragment.module);
  if (vertexModule == nullptr || fragmentModule == nullptr) {
    LOGE("WebGPU: Vertex or fragment shader module is null");
    return false;
  }
  if (vertexModule->webgpuShaderModule() == nullptr ||
      fragmentModule->webgpuShaderModule() == nullptr) {
    LOGE("WebGPU: Shader compilation failed, cannot create pipeline");
    return false;
  }

  cullMode = ToWGPUCullMode(descriptor.primitive.cullMode);
  frontFace = ToWGPUFrontFace(descriptor.primitive.frontFace);

  // Build binding layout entries for BindGroupLayout.
  std::vector<WGPUBindGroupLayoutEntry> layoutEntries = {};

  for (auto& entry : descriptor.layout.uniformBlocks) {
    WGPUBindGroupLayoutEntry layoutEntry = {};
    layoutEntry.binding = entry.binding;
    layoutEntry.visibility = WGPUShaderStage_None;
    if (entry.visibility & ShaderVisibility::Vertex) {
      layoutEntry.visibility |= WGPUShaderStage_Vertex;
    }
    if (entry.visibility & ShaderVisibility::Fragment) {
      layoutEntry.visibility |= WGPUShaderStage_Fragment;
    }
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntry.buffer.hasDynamicOffset = false;
    layoutEntry.buffer.minBindingSize = 0;
    layoutEntries.push_back(layoutEntry);
    uniformBlockVisibility[entry.binding] = entry.visibility;
  }

  for (auto& entry : descriptor.layout.textureSamplers) {
    // In the GLSL→WGSL conversion, combined samplers are split into separate texture and sampler
    // resources. The GLSL binding numbers are reassigned starting from TEXTURE_BINDING_POINT_START
    // (after uniform blocks). In WGSL, each combined sampler occupies two consecutive bindings:
    //   texture = TEXTURE_BINDING_POINT_START + samplerIndex * 2
    //   sampler = texture_binding + 1
    auto samplerIndex = static_cast<unsigned>(entry.binding < TEXTURE_BINDING_POINT_START
                                                  ? entry.binding
                                                  : entry.binding - TEXTURE_BINDING_POINT_START);
    unsigned textureBinding = TEXTURE_BINDING_POINT_START + samplerIndex * 2;
    unsigned samplerBinding = textureBinding + 1;
    WGPUBindGroupLayoutEntry textureEntry = {};
    textureEntry.binding = textureBinding;
    textureEntry.visibility = WGPUShaderStage_Fragment;
    textureEntry.texture.sampleType = WGPUTextureSampleType_Float;
    textureEntry.texture.viewDimension = WGPUTextureViewDimension_2D;
    textureEntry.texture.multisampled = false;
    layoutEntries.push_back(textureEntry);

    WGPUBindGroupLayoutEntry samplerLayoutEntry = {};
    samplerLayoutEntry.binding = samplerBinding;
    samplerLayoutEntry.visibility = WGPUShaderStage_Fragment;
    samplerLayoutEntry.sampler.type = WGPUSamplerBindingType_Filtering;
    layoutEntries.push_back(samplerLayoutEntry);

    textureUnits[entry.binding] = textureBinding;
  }

  WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
  bindGroupLayoutDesc.entryCount = layoutEntries.size();
  bindGroupLayoutDesc.entries = layoutEntries.data();
  _bindGroupLayout = wgpuDeviceCreateBindGroupLayout(gpu->device(), &bindGroupLayoutDesc);
  if (_bindGroupLayout == nullptr) {
    LOGE("[WebGPU Pipeline] Failed to create bind group layout");
    return false;
  }

  WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
  pipelineLayoutDesc.bindGroupLayoutCount = 1;
  pipelineLayoutDesc.bindGroupLayouts = &_bindGroupLayout;
  pipelineLayout = wgpuDeviceCreatePipelineLayout(gpu->device(), &pipelineLayoutDesc);
  if (pipelineLayout == nullptr) {
    LOGE("[WebGPU Pipeline] Failed to create pipeline layout");
    return false;
  }

  // Configure vertex state.
  std::vector<WGPUVertexBufferLayout> vertexBuffers = {};
  std::vector<std::vector<WGPUVertexAttribute>> allAttributes = {};

  uint32_t globalShaderLocation = 0;
  for (size_t i = 0; i < descriptor.vertex.bufferLayouts.size(); i++) {
    auto& layout = descriptor.vertex.bufferLayouts[i];
    std::vector<WGPUVertexAttribute> attrs = {};
    size_t offset = 0;
    for (size_t j = 0; j < layout.attributes.size(); j++) {
      WGPUVertexAttribute attr = {};
      attr.format = ToWGPUVertexFormat(layout.attributes[j].format());
      attr.offset = offset;
      attr.shaderLocation = globalShaderLocation++;
      attrs.push_back(attr);
      offset += layout.attributes[j].size();
    }
    allAttributes.push_back(std::move(attrs));
  }

  for (size_t i = 0; i < descriptor.vertex.bufferLayouts.size(); i++) {
    auto& layout = descriptor.vertex.bufferLayouts[i];
    WGPUVertexBufferLayout vbLayout = {};
    vbLayout.arrayStride = layout.stride;
    vbLayout.stepMode = (layout.stepMode == VertexStepMode::Vertex) ? WGPUVertexStepMode_Vertex
                                                                    : WGPUVertexStepMode_Instance;
    vbLayout.attributeCount = allAttributes[i].size();
    vbLayout.attributes = allAttributes[i].data();
    vertexBuffers.push_back(vbLayout);
  }

  WGPUVertexState vertexState = {};
  vertexState.module = vertexModule->webgpuShaderModule();
  vertexState.entryPoint = descriptor.vertex.entryPoint.c_str();
  vertexState.bufferCount = vertexBuffers.size();
  vertexState.buffers = vertexBuffers.data();

  // Configure fragment state.
  std::vector<WGPUColorTargetState> colorTargets = {};
  std::vector<WGPUBlendState> blendStates = {};
  blendStates.resize(descriptor.fragment.colorAttachments.size());
  for (size_t i = 0; i < descriptor.fragment.colorAttachments.size(); i++) {
    auto& attachment = descriptor.fragment.colorAttachments[i];
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = ToWGPUTextureFormat(attachment.format);
    colorTarget.writeMask = ToWGPUColorWriteMask(attachment.colorWriteMask);
    if (attachment.blendEnable) {
      blendStates[i].color.srcFactor = ToWGPUBlendFactor(attachment.srcColorBlendFactor);
      blendStates[i].color.dstFactor = ToWGPUBlendFactor(attachment.dstColorBlendFactor);
      blendStates[i].color.operation = ToWGPUBlendOperation(attachment.colorBlendOp);
      blendStates[i].alpha.srcFactor = ToWGPUBlendFactor(attachment.srcAlphaBlendFactor);
      blendStates[i].alpha.dstFactor = ToWGPUBlendFactor(attachment.dstAlphaBlendFactor);
      blendStates[i].alpha.operation = ToWGPUBlendOperation(attachment.alphaBlendOp);
      colorTarget.blend = &blendStates[i];
    }
    colorTargets.push_back(colorTarget);
  }

  WGPUFragmentState fragmentState = {};
  fragmentState.module = fragmentModule->webgpuShaderModule();
  fragmentState.entryPoint = descriptor.fragment.entryPoint.c_str();
  fragmentState.targetCount = colorTargets.size();
  fragmentState.targets = colorTargets.data();

  // Configure depth stencil state.
  WGPUDepthStencilState depthStencilState = {};
  bool hasDepthStencil = (descriptor.depthStencil.format != PixelFormat::Unknown);
  if (hasDepthStencil) {
    depthStencilState.format = ToWGPUTextureFormat(descriptor.depthStencil.format);
    depthStencilState.depthWriteEnabled = descriptor.depthStencil.depthWriteEnabled;
    depthStencilState.depthCompare = ToWGPUCompareFunction(descriptor.depthStencil.depthCompare);
    depthStencilState.stencilFront.compare =
        ToWGPUCompareFunction(descriptor.depthStencil.stencilFront.compare);
    depthStencilState.stencilFront.failOp =
        ToWGPUStencilOperation(descriptor.depthStencil.stencilFront.failOp);
    depthStencilState.stencilFront.depthFailOp =
        ToWGPUStencilOperation(descriptor.depthStencil.stencilFront.depthFailOp);
    depthStencilState.stencilFront.passOp =
        ToWGPUStencilOperation(descriptor.depthStencil.stencilFront.passOp);
    depthStencilState.stencilBack.compare =
        ToWGPUCompareFunction(descriptor.depthStencil.stencilBack.compare);
    depthStencilState.stencilBack.failOp =
        ToWGPUStencilOperation(descriptor.depthStencil.stencilBack.failOp);
    depthStencilState.stencilBack.depthFailOp =
        ToWGPUStencilOperation(descriptor.depthStencil.stencilBack.depthFailOp);
    depthStencilState.stencilBack.passOp =
        ToWGPUStencilOperation(descriptor.depthStencil.stencilBack.passOp);
    depthStencilState.stencilReadMask = descriptor.depthStencil.stencilReadMask;
    depthStencilState.stencilWriteMask = descriptor.depthStencil.stencilWriteMask;
  }

  WGPURenderPipelineDescriptor pipelineDesc = {};
  pipelineDesc.layout = pipelineLayout;
  pipelineDesc.vertex = vertexState;
  pipelineDesc.fragment = &fragmentState;
  pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
  pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
  pipelineDesc.primitive.frontFace = frontFace;
  pipelineDesc.primitive.cullMode = cullMode;
  if (hasDepthStencil) {
    pipelineDesc.depthStencil = &depthStencilState;
  }
  pipelineDesc.multisample.count = static_cast<uint32_t>(descriptor.multisample.count);
  pipelineDesc.multisample.mask = descriptor.multisample.mask;
  pipelineDesc.multisample.alphaToCoverageEnabled = descriptor.multisample.alphaToCoverageEnabled;

  // Push error scope to capture validation errors from pipeline creation.
  wgpuDevicePushErrorScope(gpu->device(), WGPUErrorFilter_Validation);
  pipeline = wgpuDeviceCreateRenderPipeline(gpu->device(), &pipelineDesc);
  wgpuDevicePopErrorScope(gpu->device(), OnPipelineErrorScope, nullptr);

  if (pipeline == nullptr) {
    return false;
  }

  // Also create a TriangleStrip variant since WebGPU requires topology at pipeline creation time,
  // but TGFX specifies it at draw time. Many draw calls use TriangleStrip(4 vertices) for quads.
  pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleStrip;
  pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Uint16;
  wgpuDevicePushErrorScope(gpu->device(), WGPUErrorFilter_Validation);
  pipelineStrip = wgpuDeviceCreateRenderPipeline(gpu->device(), &pipelineDesc);
  wgpuDevicePopErrorScope(gpu->device(), OnPipelineErrorScope, nullptr);
  if (pipelineStrip == nullptr) {
    LOGE(
        "[WebGPU] Failed to create TriangleStrip pipeline variant, will fall back to TriangleList");
  }
  return true;
}

unsigned WebGPURenderPipeline::getTextureIndex(unsigned binding) const {
  auto it = textureUnits.find(binding);
  if (it != textureUnits.end()) {
    return it->second;
  }
  return binding;
}

uint32_t WebGPURenderPipeline::getUniformBlockVisibility(unsigned binding) const {
  auto it = uniformBlockVisibility.find(binding);
  if (it != uniformBlockVisibility.end()) {
    return it->second;
  }
  return ShaderVisibility::VertexFragment;
}

void WebGPURenderPipeline::onRelease(WebGPUGPU*) {
  if (pipeline != nullptr) {
    wgpuRenderPipelineRelease(pipeline);
    pipeline = nullptr;
  }
  if (pipelineStrip != nullptr) {
    wgpuRenderPipelineRelease(pipelineStrip);
    pipelineStrip = nullptr;
  }
  if (_bindGroupLayout != nullptr) {
    wgpuBindGroupLayoutRelease(_bindGroupLayout);
    _bindGroupLayout = nullptr;
  }
  if (pipelineLayout != nullptr) {
    wgpuPipelineLayoutRelease(pipelineLayout);
    pipelineLayout = nullptr;
  }
}

}  // namespace tgfx
