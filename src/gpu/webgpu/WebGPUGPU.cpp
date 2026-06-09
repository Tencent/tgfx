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

std::unique_ptr<WebGPUGPU> WebGPUGPU::Make(WGPUDevice device) {
  if (device == nullptr) {
    return nullptr;
  }
  return std::unique_ptr<WebGPUGPU>(new WebGPUGPU(device));
}

WebGPUGPU::WebGPUGPU(WGPUDevice device) : webgpuDevice(device) {
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
  if (releaseGPU && webgpuDevice != nullptr) {
    wgpuDeviceRelease(webgpuDevice);
    webgpuDevice = nullptr;
  }
}

}  // namespace tgfx
