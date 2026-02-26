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
#include "WebGPUGPU.h"
#include "WebGPUBuffer.h"
#include "WebGPUCommandEncoder.h"
#include "WebGPUCommandQueue.h"
#include "WebGPUExternalTexture.h"
#include "WebGPURenderPipeline.h"
#include "WebGPUSampler.h"
#include "WebGPUShaderModule.h"
#include "WebGPUTexture.h"
#include "WebGPUUtil.h"
#include "core/utils/Log.h"

namespace tgfx {

static wgpu::BufferUsage GetWebGPUBufferUsage(uint32_t usage) {
  wgpu::BufferUsage wgpuUsage = wgpu::BufferUsage::CopyDst;
  if (usage & GPUBufferUsage::VERTEX) {
    wgpuUsage |= wgpu::BufferUsage::Vertex;
  }
  if (usage & GPUBufferUsage::INDEX) {
    wgpuUsage |= wgpu::BufferUsage::Index;
  }
  if (usage & GPUBufferUsage::UNIFORM) {
    wgpuUsage |= wgpu::BufferUsage::Uniform;
  }
  if (usage & GPUBufferUsage::READBACK) {
    wgpuUsage |= wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopySrc;
  }
  return wgpuUsage;
}

WebGPUGPU::WebGPUGPU(wgpu::Instance instance, wgpu::Adapter adapter, wgpu::Device device,
                     std::shared_ptr<WebGPUCaps> caps)
    : _instance(instance), _adapter(adapter), _device(device), _queue(device.GetQueue()),
      webGPUCaps(std::move(caps)) {
  commandQueue = std::make_unique<WebGPUCommandQueue>(this);
}

std::vector<std::shared_ptr<Texture>> WebGPUGPU::importHardwareTextures(HardwareBufferRef,
                                                                        uint32_t) {
  return {};
}

std::shared_ptr<GPUBuffer> WebGPUGPU::createBuffer(size_t size, uint32_t usage) {
  if (size == 0) {
    return nullptr;
  }
  if (usage == 0) {
    LOGE("WebGPUGPU::createBuffer() invalid buffer usage!");
    return nullptr;
  }

  wgpu::BufferDescriptor descriptor = {};
  descriptor.size = size;
  descriptor.usage = GetWebGPUBufferUsage(usage);
  descriptor.mappedAtCreation = false;

  wgpu::Buffer buffer = _device.CreateBuffer(&descriptor);
  if (buffer == nullptr) {
    LOGE("WebGPUGPU::createBuffer() failed to create buffer!");
    return nullptr;
  }
  return std::make_shared<WebGPUBuffer>(buffer, _queue, size, usage);
}

std::shared_ptr<Texture> WebGPUGPU::createTexture(const TextureDescriptor& descriptor) {
  if (descriptor.width <= 0 || descriptor.height <= 0 ||
      descriptor.format == PixelFormat::Unknown || descriptor.mipLevelCount < 1 ||
      descriptor.sampleCount < 1 || descriptor.usage == 0) {
    LOGE("WebGPUGPU::createTexture() invalid texture descriptor!");
    return nullptr;
  }
  if (descriptor.usage & TextureUsage::RENDER_ATTACHMENT &&
      !isFormatRenderable(descriptor.format)) {
    LOGE(
        "WebGPUGPU::createTexture() format is not renderable, but usage includes "
        "RENDER_ATTACHMENT!");
    return nullptr;
  }

  auto wgpuFormat = ToWGPUTextureFormat(descriptor.format);
  if (wgpuFormat == wgpu::TextureFormat::Undefined) {
    LOGE("WebGPUGPU::createTexture() unsupported pixel format!");
    return nullptr;
  }

  wgpu::TextureDescriptor textureDesc = {};
  textureDesc.size.width = static_cast<uint32_t>(descriptor.width);
  textureDesc.size.height = static_cast<uint32_t>(descriptor.height);
  textureDesc.size.depthOrArrayLayers = 1;
  textureDesc.mipLevelCount = static_cast<uint32_t>(descriptor.mipLevelCount);
  textureDesc.sampleCount = static_cast<uint32_t>(descriptor.sampleCount);
  textureDesc.dimension = wgpu::TextureDimension::e2D;
  textureDesc.format = wgpuFormat;
  textureDesc.usage = ToWGPUTextureUsage(descriptor.usage);

  wgpu::Texture texture = _device.CreateTexture(&textureDesc);
  if (texture == nullptr) {
    LOGE("WebGPUGPU::createTexture() failed to create texture!");
    return nullptr;
  }

  return std::make_shared<WebGPUTexture>(descriptor, texture);
}

std::shared_ptr<Texture> WebGPUGPU::importBackendTexture(const BackendTexture& backendTexture,
                                                         uint32_t usage, bool adopted) {
  WebGPUTextureInfo textureInfo = {};
  if (!backendTexture.getWebGPUTextureInfo(&textureInfo)) {
    return nullptr;
  }
  if (textureInfo.texture == nullptr) {
    LOGE("WebGPUGPU::importBackendTexture() texture is null!");
    return nullptr;
  }
  auto format = backendTexture.format();
  if (format == PixelFormat::Unknown) {
    LOGE("WebGPUGPU::importBackendTexture() unsupported texture format!");
    return nullptr;
  }
  if (usage & TextureUsage::RENDER_ATTACHMENT && !isFormatRenderable(format)) {
    LOGE("WebGPUGPU::importBackendTexture() format is not renderable!");
    return nullptr;
  }
  TextureDescriptor descriptor = {
      backendTexture.width(), backendTexture.height(), format, false, 1, usage};
  wgpu::Texture texture =
      wgpu::Texture::Acquire(static_cast<WGPUTexture>(const_cast<void*>(textureInfo.texture)));
  if (adopted) {
    return std::make_shared<WebGPUTexture>(descriptor, texture);
  }
  return std::make_shared<WebGPUExternalTexture>(descriptor, texture);
}

std::shared_ptr<Texture> WebGPUGPU::importBackendRenderTarget(
    const BackendRenderTarget& renderTarget) {
  WebGPUTextureInfo textureInfo = {};
  if (!renderTarget.getWebGPUTextureInfo(&textureInfo)) {
    return nullptr;
  }
  if (textureInfo.texture == nullptr) {
    LOGE("WebGPUGPU::importBackendRenderTarget() texture is null!");
    return nullptr;
  }
  auto format = renderTarget.format();
  if (format == PixelFormat::Unknown) {
    LOGE("WebGPUGPU::importBackendRenderTarget() unsupported texture format!");
    return nullptr;
  }
  if (!isFormatRenderable(format)) {
    LOGE("WebGPUGPU::importBackendRenderTarget() format is not renderable!");
    return nullptr;
  }
  TextureDescriptor descriptor = {
      renderTarget.width(),           renderTarget.height(), format, false, 1,
      TextureUsage::RENDER_ATTACHMENT};
  wgpu::Texture texture =
      wgpu::Texture::Acquire(static_cast<WGPUTexture>(const_cast<void*>(textureInfo.texture)));
  return std::make_shared<WebGPUExternalTexture>(descriptor, texture);
}

std::shared_ptr<Semaphore> WebGPUGPU::importBackendSemaphore(const BackendSemaphore&) {
  // WebGPU uses an implicit synchronization model and does not expose explicit synchronization
  // primitives like semaphores.
  return nullptr;
}

BackendSemaphore WebGPUGPU::stealBackendSemaphore(std::shared_ptr<Semaphore>) {
  // WebGPU uses an implicit synchronization model and does not expose explicit synchronization
  // primitives like semaphores.
  return {};
}

std::shared_ptr<Sampler> WebGPUGPU::createSampler(const SamplerDescriptor& descriptor) {
  wgpu::SamplerDescriptor samplerDesc = {};
  samplerDesc.addressModeU = ToWGPUAddressMode(descriptor.addressModeX);
  samplerDesc.addressModeV = ToWGPUAddressMode(descriptor.addressModeY);
  samplerDesc.addressModeW = wgpu::AddressMode::ClampToEdge;
  samplerDesc.minFilter = ToWGPUFilterMode(descriptor.minFilter);
  samplerDesc.magFilter = ToWGPUFilterMode(descriptor.magFilter);
  samplerDesc.mipmapFilter = ToWGPUMipmapFilterMode(descriptor.mipmapMode);
  samplerDesc.lodMinClamp = 0.0f;
  samplerDesc.lodMaxClamp = (descriptor.mipmapMode == MipmapMode::None) ? 0.0f : 32.0f;
  samplerDesc.maxAnisotropy = 1;

  wgpu::Sampler sampler = _device.CreateSampler(&samplerDesc);
  if (sampler == nullptr) {
    LOGE("WebGPUGPU::createSampler() failed to create sampler!");
    return nullptr;
  }
  return std::make_shared<WebGPUSampler>(sampler);
}

std::shared_ptr<ShaderModule> WebGPUGPU::createShaderModule(
    const ShaderModuleDescriptor& descriptor) {
  if (descriptor.code.empty()) {
    LOGE("WebGPUGPU::createShaderModule() shader code is empty!");
    return nullptr;
  }

  wgpu::ShaderModuleWGSLDescriptor wgslDescriptor = {};
  wgslDescriptor.code = descriptor.code.c_str();

  wgpu::ShaderModuleDescriptor shaderModuleDesc = {};
  shaderModuleDesc.nextInChain = &wgslDescriptor;

  wgpu::ShaderModule shaderModule = _device.CreateShaderModule(&shaderModuleDesc);
  if (shaderModule == nullptr) {
    LOGE("WebGPUGPU::createShaderModule() failed to create shader module!");
    return nullptr;
  }
  return std::make_shared<WebGPUShaderModule>(shaderModule);
}

std::shared_ptr<RenderPipeline> WebGPUGPU::createRenderPipeline(
    const RenderPipelineDescriptor& descriptor) {
  auto vertexModule = static_cast<WebGPUShaderModule*>(descriptor.vertex.module.get());
  auto fragmentModule = static_cast<WebGPUShaderModule*>(descriptor.fragment.module.get());
  if (vertexModule == nullptr || vertexModule->wgpuShaderModule() == nullptr) {
    LOGE("WebGPUGPU::createRenderPipeline() invalid vertex shader module!");
    return nullptr;
  }
  if (fragmentModule == nullptr || fragmentModule->wgpuShaderModule() == nullptr) {
    LOGE("WebGPUGPU::createRenderPipeline() invalid fragment shader module!");
    return nullptr;
  }
  if (descriptor.vertex.bufferLayouts.empty()) {
    LOGE("WebGPUGPU::createRenderPipeline() invalid vertex buffer layouts, no layouts set!");
    return nullptr;
  }
  if (descriptor.vertex.bufferLayouts[0].stride == 0) {
    LOGE("WebGPUGPU::createRenderPipeline() invalid vertex buffer layout, stride is 0!");
    return nullptr;
  }
  if (descriptor.fragment.colorAttachments.empty()) {
    LOGE("WebGPUGPU::createRenderPipeline() invalid color attachments, no color attachments!");
    return nullptr;
  }

  // Build vertex attributes from first buffer layout
  const auto& bufferLayout = descriptor.vertex.bufferLayouts[0];
  std::vector<wgpu::VertexAttribute> vertexAttributes;
  vertexAttributes.reserve(bufferLayout.attributes.size());
  size_t vertexOffset = 0;
  uint32_t shaderLocation = 0;
  for (const auto& attr : bufferLayout.attributes) {
    wgpu::VertexAttribute wgpuAttr = {};
    wgpuAttr.format = ToWGPUVertexFormat(attr.format());
    wgpuAttr.offset = vertexOffset;
    wgpuAttr.shaderLocation = shaderLocation++;
    vertexAttributes.push_back(wgpuAttr);
    vertexOffset += attr.size();
  }

  // Build vertex buffer layout
  wgpu::VertexBufferLayout vertexBufferLayout = {};
  vertexBufferLayout.arrayStride = bufferLayout.stride;
  vertexBufferLayout.stepMode = wgpu::VertexStepMode::Vertex;
  vertexBufferLayout.attributeCount = vertexAttributes.size();
  vertexBufferLayout.attributes = vertexAttributes.data();

  // Build vertex state
  wgpu::VertexState vertexState = {};
  vertexState.module = vertexModule->wgpuShaderModule();
  vertexState.entryPoint = descriptor.vertex.entryPoint.c_str();
  vertexState.bufferCount = 1;
  vertexState.buffers = &vertexBufferLayout;

  // Build color target states and blend states
  std::vector<wgpu::ColorTargetState> colorTargets;
  std::vector<wgpu::BlendState> blendStates;
  colorTargets.reserve(descriptor.fragment.colorAttachments.size());
  blendStates.reserve(descriptor.fragment.colorAttachments.size());

  for (const auto& attachment : descriptor.fragment.colorAttachments) {
    wgpu::ColorTargetState colorTarget = {};
    colorTarget.format = ToWGPUTextureFormat(attachment.format);

    if (attachment.blendEnable) {
      wgpu::BlendState blendState = {};
      blendState.color.srcFactor = ToWGPUBlendFactor(attachment.srcColorBlendFactor);
      blendState.color.dstFactor = ToWGPUBlendFactor(attachment.dstColorBlendFactor);
      blendState.color.operation = ToWGPUBlendOperation(attachment.colorBlendOp);
      blendState.alpha.srcFactor = ToWGPUBlendFactor(attachment.srcAlphaBlendFactor);
      blendState.alpha.dstFactor = ToWGPUBlendFactor(attachment.dstAlphaBlendFactor);
      blendState.alpha.operation = ToWGPUBlendOperation(attachment.alphaBlendOp);
      blendStates.push_back(blendState);
      colorTarget.blend = &blendStates.back();
    }

    colorTarget.writeMask = static_cast<wgpu::ColorWriteMask>(attachment.colorWriteMask);
    colorTargets.push_back(colorTarget);
  }

  // Build fragment state
  wgpu::FragmentState fragmentState = {};
  fragmentState.module = fragmentModule->wgpuShaderModule();
  fragmentState.entryPoint = descriptor.fragment.entryPoint.c_str();
  fragmentState.targetCount = colorTargets.size();
  fragmentState.targets = colorTargets.data();

  // Build depth stencil state if needed
  wgpu::DepthStencilState depthStencilState = {};
  bool hasDepthStencil = false;
  auto& depthStencil = descriptor.depthStencil;
  bool hasStencil = depthStencil.stencilFront.compare != CompareFunction::Always ||
                    depthStencil.stencilBack.compare != CompareFunction::Always;
  bool hasDepth =
      depthStencil.depthCompare != CompareFunction::Always || depthStencil.depthWriteEnabled;

  if (hasDepth || hasStencil) {
    hasDepthStencil = true;
    depthStencilState.format = wgpu::TextureFormat::Depth24PlusStencil8;
    depthStencilState.depthWriteEnabled = depthStencil.depthWriteEnabled;
    depthStencilState.depthCompare = ToWGPUCompareFunction(depthStencil.depthCompare);

    // Front face stencil
    depthStencilState.stencilFront.compare =
        ToWGPUCompareFunction(depthStencil.stencilFront.compare);
    depthStencilState.stencilFront.failOp =
        ToWGPUStencilOperation(depthStencil.stencilFront.failOp);
    depthStencilState.stencilFront.depthFailOp =
        ToWGPUStencilOperation(depthStencil.stencilFront.depthFailOp);
    depthStencilState.stencilFront.passOp =
        ToWGPUStencilOperation(depthStencil.stencilFront.passOp);

    // Back face stencil
    depthStencilState.stencilBack.compare = ToWGPUCompareFunction(depthStencil.stencilBack.compare);
    depthStencilState.stencilBack.failOp = ToWGPUStencilOperation(depthStencil.stencilBack.failOp);
    depthStencilState.stencilBack.depthFailOp =
        ToWGPUStencilOperation(depthStencil.stencilBack.depthFailOp);
    depthStencilState.stencilBack.passOp = ToWGPUStencilOperation(depthStencil.stencilBack.passOp);

    depthStencilState.stencilReadMask = depthStencil.stencilReadMask;
    depthStencilState.stencilWriteMask = depthStencil.stencilWriteMask;
  }

  // Build primitive state
  wgpu::PrimitiveState primitiveState = {};
  primitiveState.topology = wgpu::PrimitiveTopology::TriangleList;
  primitiveState.frontFace = ToWGPUFrontFace(descriptor.primitive.frontFace);
  primitiveState.cullMode = ToWGPUCullMode(descriptor.primitive.cullMode);

  // Build bind group layout entries for uniform blocks and texture samplers
  std::vector<wgpu::BindGroupLayoutEntry> bindGroupLayoutEntries;
  std::unordered_map<unsigned, unsigned> textureBindingMap;

  for (const auto& entry : descriptor.layout.uniformBlocks) {
    wgpu::BindGroupLayoutEntry layoutEntry = {};
    layoutEntry.binding = entry.binding;
    layoutEntry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    layoutEntry.buffer.type = wgpu::BufferBindingType::Uniform;
    layoutEntry.buffer.hasDynamicOffset = false;
    layoutEntry.buffer.minBindingSize = 0;
    bindGroupLayoutEntries.push_back(layoutEntry);
  }

  for (const auto& entry : descriptor.layout.textureSamplers) {
    textureBindingMap[entry.binding] = entry.binding;

    // Texture binding
    wgpu::BindGroupLayoutEntry textureEntry = {};
    textureEntry.binding = entry.binding;
    textureEntry.visibility = wgpu::ShaderStage::Fragment;
    textureEntry.texture.sampleType = wgpu::TextureSampleType::Float;
    textureEntry.texture.viewDimension = wgpu::TextureViewDimension::e2D;
    textureEntry.texture.multisampled = false;
    bindGroupLayoutEntries.push_back(textureEntry);

    // Sampler binding (use binding + 100 to avoid collision with texture bindings)
    wgpu::BindGroupLayoutEntry samplerEntry = {};
    samplerEntry.binding = entry.binding + 100;
    samplerEntry.visibility = wgpu::ShaderStage::Fragment;
    samplerEntry.sampler.type = wgpu::SamplerBindingType::Filtering;
    bindGroupLayoutEntries.push_back(samplerEntry);
  }

  // Create bind group layout
  wgpu::BindGroupLayoutDescriptor bindGroupLayoutDesc = {};
  bindGroupLayoutDesc.entryCount = bindGroupLayoutEntries.size();
  bindGroupLayoutDesc.entries = bindGroupLayoutEntries.data();
  wgpu::BindGroupLayout bindGroupLayout = _device.CreateBindGroupLayout(&bindGroupLayoutDesc);
  if (bindGroupLayout == nullptr) {
    LOGE("WebGPUGPU::createRenderPipeline() failed to create bind group layout!");
    return nullptr;
  }

  // Create pipeline layout
  wgpu::PipelineLayoutDescriptor pipelineLayoutDesc = {};
  pipelineLayoutDesc.bindGroupLayoutCount = 1;
  pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout;
  wgpu::PipelineLayout pipelineLayout = _device.CreatePipelineLayout(&pipelineLayoutDesc);
  if (pipelineLayout == nullptr) {
    LOGE("WebGPUGPU::createRenderPipeline() failed to create pipeline layout!");
    return nullptr;
  }

  // Create render pipeline descriptor
  wgpu::RenderPipelineDescriptor pipelineDesc = {};
  pipelineDesc.layout = pipelineLayout;
  pipelineDesc.vertex = vertexState;
  pipelineDesc.fragment = &fragmentState;
  pipelineDesc.primitive = primitiveState;
  if (hasDepthStencil) {
    pipelineDesc.depthStencil = &depthStencilState;
  }

  // Multisample state (default to 1 sample)
  wgpu::MultisampleState multisampleState = {};
  multisampleState.count = 1;
  multisampleState.mask = ~0u;
  multisampleState.alphaToCoverageEnabled = false;
  pipelineDesc.multisample = multisampleState;

  wgpu::RenderPipeline pipeline = _device.CreateRenderPipeline(&pipelineDesc);
  if (pipeline == nullptr) {
    LOGE("WebGPUGPU::createRenderPipeline() failed to create render pipeline!");
    return nullptr;
  }

  return std::make_shared<WebGPURenderPipeline>(std::move(pipeline), std::move(bindGroupLayout),
                                                std::move(textureBindingMap),
                                                bufferLayout.stride);
}

std::shared_ptr<CommandEncoder> WebGPUGPU::createCommandEncoder() {
  return std::make_shared<WebGPUCommandEncoder>(this);
}

static const char* MipmapShaderCode = R"(
@group(0) @binding(0) var srcTexture: texture_2d<f32>;
@group(0) @binding(1) var srcSampler: sampler;

struct VertexOutput {
  @builtin(position) position: vec4f,
  @location(0) texCoord: vec2f,
};

@vertex fn vs(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
  // Generate fullscreen triangle positions and UVs
  var pos = array<vec2f, 3>(
    vec2f(-1.0, -1.0),
    vec2f(3.0, -1.0),
    vec2f(-1.0, 3.0)
  );
  var uv = array<vec2f, 3>(
    vec2f(0.0, 1.0),
    vec2f(2.0, 1.0),
    vec2f(0.0, -1.0)
  );
  var output: VertexOutput;
  output.position = vec4f(pos[vertexIndex], 0.0, 1.0);
  output.texCoord = uv[vertexIndex];
  return output;
}

@fragment fn fs(input: VertexOutput) -> @location(0) vec4f {
  return textureSample(srcTexture, srcSampler, input.texCoord);
}
)";

void WebGPUGPU::initMipmapResources() {
  if (mipmapShaderModule != nullptr) {
    return;
  }

  // Create shader module
  wgpu::ShaderModuleWGSLDescriptor wgslDesc = {};
  wgslDesc.code = MipmapShaderCode;
  wgpu::ShaderModuleDescriptor shaderDesc = {};
  shaderDesc.nextInChain = &wgslDesc;
  mipmapShaderModule = _device.CreateShaderModule(&shaderDesc);

  // Create sampler with bilinear filtering
  wgpu::SamplerDescriptor samplerDesc = {};
  samplerDesc.minFilter = wgpu::FilterMode::Linear;
  samplerDesc.magFilter = wgpu::FilterMode::Linear;
  samplerDesc.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
  samplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
  samplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
  mipmapSampler = _device.CreateSampler(&samplerDesc);

  // Create bind group layout
  std::array<wgpu::BindGroupLayoutEntry, 2> bindGroupLayoutEntries = {};
  bindGroupLayoutEntries[0].binding = 0;
  bindGroupLayoutEntries[0].visibility = wgpu::ShaderStage::Fragment;
  bindGroupLayoutEntries[0].texture.sampleType = wgpu::TextureSampleType::Float;
  bindGroupLayoutEntries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
  bindGroupLayoutEntries[1].binding = 1;
  bindGroupLayoutEntries[1].visibility = wgpu::ShaderStage::Fragment;
  bindGroupLayoutEntries[1].sampler.type = wgpu::SamplerBindingType::Filtering;

  wgpu::BindGroupLayoutDescriptor bindGroupLayoutDesc = {};
  bindGroupLayoutDesc.entryCount = bindGroupLayoutEntries.size();
  bindGroupLayoutDesc.entries = bindGroupLayoutEntries.data();
  mipmapBindGroupLayout = _device.CreateBindGroupLayout(&bindGroupLayoutDesc);

  // Create pipeline layout
  wgpu::PipelineLayoutDescriptor pipelineLayoutDesc = {};
  pipelineLayoutDesc.bindGroupLayoutCount = 1;
  pipelineLayoutDesc.bindGroupLayouts = &mipmapBindGroupLayout;
  mipmapPipelineLayout = _device.CreatePipelineLayout(&pipelineLayoutDesc);
}

wgpu::RenderPipeline WebGPUGPU::getMipmapPipeline(PixelFormat format) {
  initMipmapResources();

  auto it = mipmapPipelines.find(format);
  if (it != mipmapPipelines.end()) {
    return it->second;
  }

  auto wgpuFormat = ToWGPUTextureFormat(format);
  if (wgpuFormat == wgpu::TextureFormat::Undefined) {
    return nullptr;
  }

  // Build vertex state (no vertex buffers needed for fullscreen triangle)
  wgpu::VertexState vertexState = {};
  vertexState.module = mipmapShaderModule;
  vertexState.entryPoint = "vs";
  vertexState.bufferCount = 0;
  vertexState.buffers = nullptr;

  // Build color target state
  wgpu::ColorTargetState colorTarget = {};
  colorTarget.format = wgpuFormat;
  colorTarget.writeMask = wgpu::ColorWriteMask::All;

  // Build fragment state
  wgpu::FragmentState fragmentState = {};
  fragmentState.module = mipmapShaderModule;
  fragmentState.entryPoint = "fs";
  fragmentState.targetCount = 1;
  fragmentState.targets = &colorTarget;

  // Build primitive state
  wgpu::PrimitiveState primitiveState = {};
  primitiveState.topology = wgpu::PrimitiveTopology::TriangleList;
  primitiveState.frontFace = wgpu::FrontFace::CCW;
  primitiveState.cullMode = wgpu::CullMode::None;

  // Create render pipeline
  wgpu::RenderPipelineDescriptor pipelineDesc = {};
  pipelineDesc.layout = mipmapPipelineLayout;
  pipelineDesc.vertex = vertexState;
  pipelineDesc.fragment = &fragmentState;
  pipelineDesc.primitive = primitiveState;

  wgpu::RenderPipeline pipeline = _device.CreateRenderPipeline(&pipelineDesc);
  mipmapPipelines[format] = pipeline;
  return pipeline;
}

wgpu::Sampler WebGPUGPU::getMipmapSampler() {
  initMipmapResources();
  return mipmapSampler;
}

wgpu::BindGroupLayout WebGPUGPU::getMipmapBindGroupLayout() {
  initMipmapResources();
  return mipmapBindGroupLayout;
}

}  // namespace tgfx
