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
#include "WebGPURenderPipeline.h"
#include "WebGPUSampler.h"
#include "WebGPUSemaphore.h"
#include "WebGPUShaderModule.h"
#include "WebGPUTexture.h"
#include "core/utils/Log.h"

namespace tgfx {

std::unique_ptr<WebGPUGPU> WebGPUGPU::Make(WGPUDevice device, bool externallyOwned) {
  if (device == nullptr) {
    return nullptr;
  }
  return std::unique_ptr<WebGPUGPU>(new WebGPUGPU(device, externallyOwned));
}

WebGPUGPU::WebGPUGPU(WGPUDevice device, bool externallyOwned)
    : webgpuDevice(device), _externallyOwned(externallyOwned) {
  caps = std::make_unique<WebGPUCaps>(device);
  commandQueue = std::make_unique<WebGPUCommandQueue>(this);
}

WebGPUGPU::~WebGPUGPU() {
  releaseAll(false);
}

CommandQueue* WebGPUGPU::queue() const {
  return commandQueue.get();
}

std::shared_ptr<GPUBuffer> WebGPUGPU::createBuffer(size_t size, uint32_t usage) {
  return WebGPUBuffer::Make(this, size, usage);
}

std::shared_ptr<Texture> WebGPUGPU::createTexture(const TextureDescriptor& descriptor) {
  return WebGPUTexture::Make(this, descriptor);
}

std::shared_ptr<Sampler> WebGPUGPU::createSampler(const SamplerDescriptor& descriptor) {
  auto key = MakeSamplerKey(descriptor);
  auto it = samplerCache.find(key);
  if (it != samplerCache.end()) {
    return it->second;
  }
  auto sampler = WebGPUSampler::Make(this, descriptor);
  if (sampler != nullptr) {
    samplerCache[key] = sampler;
  }
  return sampler;
}

std::shared_ptr<ShaderModule> WebGPUGPU::createShaderModule(
    const ShaderModuleDescriptor& descriptor) {
  return WebGPUShaderModule::Make(this, descriptor);
}

std::shared_ptr<RenderPipeline> WebGPUGPU::createRenderPipeline(
    const RenderPipelineDescriptor& descriptor) {
  return WebGPURenderPipeline::Make(this, descriptor);
}

std::shared_ptr<CommandEncoder> WebGPUGPU::createCommandEncoder() {
  return WebGPUCommandEncoder::Make(this);
}

int WebGPUGPU::getSampleCount(int requestedCount, PixelFormat pixelFormat) const {
  return caps->getSampleCount(requestedCount, pixelFormat);
}

std::vector<std::shared_ptr<Texture>> WebGPUGPU::importHardwareTextures(HardwareBufferRef,
                                                                        uint32_t) {
  return {};
}

std::shared_ptr<Texture> WebGPUGPU::importBackendTexture(const BackendTexture& backendTexture,
                                                         uint32_t usage, bool adopted) {
  WebGPUTextureInfo textureInfo = {};
  if (!backendTexture.getWebGPUTextureInfo(&textureInfo)) {
    return nullptr;
  }
  auto wgpuTexture = static_cast<WGPUTexture>(const_cast<void*>(textureInfo.texture));
  if (wgpuTexture == nullptr) {
    return nullptr;
  }
  return WebGPUTexture::MakeFrom(this, wgpuTexture, usage, adopted);
}

std::shared_ptr<Texture> WebGPUGPU::importBackendRenderTarget(
    const BackendRenderTarget& backendRenderTarget) {
  WebGPUTextureInfo textureInfo = {};
  if (!backendRenderTarget.getWebGPUTextureInfo(&textureInfo)) {
    return nullptr;
  }
  auto wgpuTexture = static_cast<WGPUTexture>(const_cast<void*>(textureInfo.texture));
  if (wgpuTexture == nullptr) {
    return nullptr;
  }
  return WebGPUTexture::MakeFrom(
      this, wgpuTexture, TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING, false);
}

std::shared_ptr<Semaphore> WebGPUGPU::importBackendSemaphore(const BackendSemaphore&) {
  return nullptr;
}

BackendSemaphore WebGPUGPU::stealBackendSemaphore(std::shared_ptr<Semaphore>) {
  return {};
}

uint32_t WebGPUGPU::MakeSamplerKey(const SamplerDescriptor& descriptor) {
  uint32_t key = 0;
  key |= static_cast<uint32_t>(descriptor.addressModeX);
  key |= static_cast<uint32_t>(descriptor.addressModeY) << 4;
  key |= static_cast<uint32_t>(descriptor.minFilter) << 8;
  key |= static_cast<uint32_t>(descriptor.magFilter) << 12;
  key |= static_cast<uint32_t>(descriptor.mipmapMode) << 16;
  return key;
}

std::shared_ptr<WebGPUResource> WebGPUGPU::addResource(WebGPUResource* resource) {
  resources.push_back(resource);
  resource->position = std::prev(resources.end());
  return std::static_pointer_cast<WebGPUResource>(returnQueue->makeShared(resource));
}

void WebGPUGPU::processUnreferencedResources() {
  while (auto* node = returnQueue->dequeue()) {
    auto* resource = static_cast<WebGPUResource*>(node);
    resource->onRelease(this);
    resources.erase(resource->position);
    delete resource;
  }
}

void WebGPUGPU::releaseAll(bool releaseGPU) {
  processUnreferencedResources();
  for (auto* resource : resources) {
    if (releaseGPU) {
      resource->onRelease(this);
    }
  }
  resources.clear();
  samplerCache.clear();
  if (releaseGPU) {
    for (auto& [_, mp] : mipmapPipelineCache) {
      if (mp.sampler) wgpuSamplerRelease(mp.sampler);
      if (mp.pipeline) wgpuRenderPipelineRelease(mp.pipeline);
      if (mp.pipelineLayout) wgpuPipelineLayoutRelease(mp.pipelineLayout);
      if (mp.bindGroupLayout) wgpuBindGroupLayoutRelease(mp.bindGroupLayout);
      if (mp.shaderModule) wgpuShaderModuleRelease(mp.shaderModule);
    }
    mipmapPipelineCache.clear();
  }
  if (releaseGPU && !_externallyOwned && webgpuDevice != nullptr) {
    wgpuDeviceRelease(webgpuDevice);
    webgpuDevice = nullptr;
  }
}

static const char* MipmapWGSL = R"(
struct VertexOutput {
  @builtin(position) position : vec4f,
  @location(0) texCoord : vec2f,
};
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex : u32) -> VertexOutput {
  var pos = array<vec2f, 3>(vec2f(-1.0, -3.0), vec2f(-1.0, 1.0), vec2f(3.0, 1.0));
  var uv = array<vec2f, 3>(vec2f(0.0, 2.0), vec2f(0.0, 0.0), vec2f(2.0, 0.0));
  var output : VertexOutput;
  output.position = vec4f(pos[vertexIndex], 0.0, 1.0);
  output.texCoord = uv[vertexIndex];
  return output;
}
@group(0) @binding(0) var inputTexture : texture_2d<f32>;
@group(0) @binding(1) var inputSampler : sampler;
@fragment
fn fs_main(@location(0) texCoord : vec2f) -> @location(0) vec4f {
  return textureSample(inputTexture, inputSampler, texCoord);
}
)";

const WebGPUGPU::MipmapPipeline* WebGPUGPU::getMipmapPipeline(WGPUTextureFormat format) {
  auto key = static_cast<uint32_t>(format);
  auto it = mipmapPipelineCache.find(key);
  if (it != mipmapPipelineCache.end()) {
    return &it->second;
  }
  MipmapPipeline mp = {};
  WGPUShaderModuleWGSLDescriptor wgslDesc = {};
  wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
  wgslDesc.code = MipmapWGSL;
  WGPUShaderModuleDescriptor shaderDesc = {};
  shaderDesc.nextInChain = &wgslDesc.chain;
  mp.shaderModule = wgpuDeviceCreateShaderModule(webgpuDevice, &shaderDesc);
  if (mp.shaderModule == nullptr) {
    return nullptr;
  }
  WGPUBindGroupLayoutEntry bglEntries[2] = {};
  bglEntries[0].binding = 0;
  bglEntries[0].visibility = WGPUShaderStage_Fragment;
  bglEntries[0].texture.sampleType = WGPUTextureSampleType_Float;
  bglEntries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
  bglEntries[1].binding = 1;
  bglEntries[1].visibility = WGPUShaderStage_Fragment;
  bglEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;
  WGPUBindGroupLayoutDescriptor bglDesc = {};
  bglDesc.entryCount = 2;
  bglDesc.entries = bglEntries;
  mp.bindGroupLayout = wgpuDeviceCreateBindGroupLayout(webgpuDevice, &bglDesc);
  if (mp.bindGroupLayout == nullptr) {
    wgpuShaderModuleRelease(mp.shaderModule);
    return nullptr;
  }
  WGPUPipelineLayoutDescriptor plDesc = {};
  plDesc.bindGroupLayoutCount = 1;
  plDesc.bindGroupLayouts = &mp.bindGroupLayout;
  mp.pipelineLayout = wgpuDeviceCreatePipelineLayout(webgpuDevice, &plDesc);
  if (mp.pipelineLayout == nullptr) {
    wgpuBindGroupLayoutRelease(mp.bindGroupLayout);
    wgpuShaderModuleRelease(mp.shaderModule);
    return nullptr;
  }
  WGPUColorTargetState colorTarget = {};
  colorTarget.format = format;
  colorTarget.writeMask = WGPUColorWriteMask_All;
  WGPUFragmentState fragmentState = {};
  fragmentState.module = mp.shaderModule;
  fragmentState.entryPoint = "fs_main";
  fragmentState.targetCount = 1;
  fragmentState.targets = &colorTarget;
  WGPURenderPipelineDescriptor rpDesc = {};
  rpDesc.layout = mp.pipelineLayout;
  rpDesc.vertex.module = mp.shaderModule;
  rpDesc.vertex.entryPoint = "vs_main";
  rpDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
  rpDesc.fragment = &fragmentState;
  rpDesc.multisample.count = 1;
  rpDesc.multisample.mask = 0xFFFFFFFF;
  mp.pipeline = wgpuDeviceCreateRenderPipeline(webgpuDevice, &rpDesc);
  if (mp.pipeline == nullptr) {
    wgpuPipelineLayoutRelease(mp.pipelineLayout);
    wgpuBindGroupLayoutRelease(mp.bindGroupLayout);
    wgpuShaderModuleRelease(mp.shaderModule);
    return nullptr;
  }
  WGPUSamplerDescriptor samplerDesc = {};
  samplerDesc.minFilter = WGPUFilterMode_Linear;
  samplerDesc.magFilter = WGPUFilterMode_Linear;
  samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
  samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
  samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
  mp.sampler = wgpuDeviceCreateSampler(webgpuDevice, &samplerDesc);
  if (mp.sampler == nullptr) {
    wgpuRenderPipelineRelease(mp.pipeline);
    wgpuPipelineLayoutRelease(mp.pipelineLayout);
    wgpuBindGroupLayoutRelease(mp.bindGroupLayout);
    wgpuShaderModuleRelease(mp.shaderModule);
    return nullptr;
  }
  mipmapPipelineCache[key] = mp;
  return &mipmapPipelineCache[key];
}

}  // namespace tgfx
