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

#include "MetalGPU.h"
#include "MetalCommandQueue.h"
#include "MetalCommandEncoder.h"
#include "MetalBuffer.h"
#include "MetalTexture.h"
#include "MetalHardwareTexture.h"
#include "MetalSampler.h"
#include "MetalSemaphore.h"
#include "MetalShaderModule.h"
#include "MetalRenderPipeline.h"
#include "core/utils/Log.h"

namespace tgfx {

bool HardwareBufferAvailable() {
  return true;
}

std::unique_ptr<MetalGPU> MetalGPU::Make(id<MTLDevice> device) {
  if (device == nil) {
    return nullptr;
  }
  
  auto gpu = std::unique_ptr<MetalGPU>(new MetalGPU(device));
  if (gpu->commandQueue->metalCommandQueue() == nil) {
    return nullptr;
  }
  
  return gpu;
}

MetalGPU::MetalGPU(id<MTLDevice> device) : metalDevice([device retain]) {
  // Initialize capabilities
  caps = std::make_unique<MetalCaps>(device);
  
  // Initialize command queue
  commandQueue = std::make_unique<MetalCommandQueue>(this);
}

MetalGPU::~MetalGPU() {
  // Releases all managed resources. The caller must ensure that all external shared_ptr references
  // to resources created by this GPU have been released before calling this method, otherwise those
  // shared_ptrs will dangle.
  DEBUG_ASSERT(returnQueue == nullptr);
  DEBUG_ASSERT(resources.empty());
  if (textureCache != nil) {
    CFRelease(textureCache);
  }
  if (metalDevice != nil) {
    [metalDevice release];
    metalDevice = nil;
  }
}

CommandQueue* MetalGPU::queue() const {
  return commandQueue.get();
}

std::shared_ptr<GPUBuffer> MetalGPU::createBuffer(size_t size, uint32_t usage) {
  if (size == 0) {
    return nullptr;
  }
  @autoreleasepool {
    return MetalBuffer::Make(this, size, usage);
  }
}

std::shared_ptr<Texture> MetalGPU::createTexture(const TextureDescriptor& descriptor) {
  if (descriptor.width <= 0 || descriptor.height <= 0) {
    LOGE("MetalGPU::createTexture() invalid dimensions: %dx%d", descriptor.width, descriptor.height);
    return nullptr;
  }
  
  // Validate format support
  if (!isFormatRenderable(descriptor.format) && 
      (descriptor.usage & TextureUsage::RENDER_ATTACHMENT)) {
    LOGE("MetalGPU::createTexture() format not renderable for render attachment");
    return nullptr;
  }
  @autoreleasepool {
    auto texture = MetalTexture::Make(this, descriptor);
    if (!texture) {
      LOGE("MetalGPU::createTexture() MetalTexture::Make failed for %dx%d format=%d", 
           descriptor.width, descriptor.height, static_cast<int>(descriptor.format));
    }
    return texture;
  }
}

std::shared_ptr<Sampler> MetalGPU::createSampler(const SamplerDescriptor& descriptor) {
  @autoreleasepool {
    return MetalSampler::Make(this, descriptor);
  }
}

std::shared_ptr<ShaderModule> MetalGPU::createShaderModule(
    const ShaderModuleDescriptor& descriptor) {
  @autoreleasepool {
    return MetalShaderModule::Make(this, descriptor);
  }
}

std::shared_ptr<RenderPipeline> MetalGPU::createRenderPipeline(
    const RenderPipelineDescriptor& descriptor) {
  @autoreleasepool {
    return MetalRenderPipeline::Make(this, descriptor);
  }
}

std::shared_ptr<CommandEncoder> MetalGPU::createCommandEncoder() {
  processUnreferencedResources();
  @autoreleasepool {
    return MetalCommandEncoder::Make(this);
  }
}

int MetalGPU::getSampleCount(int requestedCount, PixelFormat pixelFormat) const {
  return caps->getSampleCount(requestedCount, pixelFormat);
}

std::vector<std::shared_ptr<Texture>> MetalGPU::importHardwareTextures(HardwareBufferRef hardwareBuffer,
                                                                     uint32_t usage) {
  return MetalHardwareTexture::MakeFrom(this, hardwareBuffer, usage, getTextureCache());
}

CVMetalTextureCacheRef MetalGPU::getTextureCache() {
  if (textureCache == nil) {
    auto status = CVMetalTextureCacheCreate(kCFAllocatorDefault, nil, metalDevice, nil, &textureCache);
    if (status != kCVReturnSuccess) {
      LOGE("MetalGPU::getTextureCache() CVMetalTextureCacheCreate failed with status: %d", status);
      return nil;
    }
  }
  return textureCache;
}

std::shared_ptr<Texture> MetalGPU::importBackendTexture(const BackendTexture& backendTexture,
                                                      uint32_t usage, bool adopted) {
  if (backendTexture.backend() != Backend::Metal) {
    return nullptr;
  }
  
  MetalTextureInfo metalInfo = {};
  if (!backendTexture.getMetalTextureInfo(&metalInfo) || metalInfo.texture == nullptr) {
    return nullptr;
  }
  
  id<MTLTexture> metalTexture = (__bridge id<MTLTexture>)metalInfo.texture;
  return MetalTexture::MakeFrom(this, metalTexture, usage, adopted);
}

std::shared_ptr<Texture> MetalGPU::importBackendRenderTarget(
    const BackendRenderTarget& backendRenderTarget) {
  MetalTextureInfo metalInfo = {};
  if (!backendRenderTarget.getMetalTextureInfo(&metalInfo)) {
    return nullptr;
  }
  auto format = backendRenderTarget.format();
  if (!isFormatRenderable(format)) {
    return nullptr;
  }
  id<MTLTexture> metalTexture = (__bridge id<MTLTexture>)metalInfo.texture;
  if (!metalTexture) {
    return nullptr;
  }
  return MetalTexture::MakeFrom(this, metalTexture, TextureUsage::RENDER_ATTACHMENT, false);
}

std::shared_ptr<Semaphore> MetalGPU::importBackendSemaphore(const BackendSemaphore& semaphore) {
  if (semaphore.backend() != Backend::Metal) {
    return nullptr;
  }
  
  MetalSemaphoreInfo metalInfo = {};
  if (!semaphore.getMetalSemaphore(&metalInfo) || metalInfo.event == nullptr) {
    return nullptr;
  }
  
  id<MTLEvent> event = (__bridge id<MTLEvent>)metalInfo.event;
  return MetalSemaphore::MakeFrom(this, event, metalInfo.value);
}

BackendSemaphore MetalGPU::stealBackendSemaphore(std::shared_ptr<Semaphore> semaphore) {
  if (semaphore == nullptr || semaphore.use_count() > 2) {
    return {};
  }
  // For Metal, we don't clear the _event because the command queue's pendingSignalSemaphore
  // still needs to signal it during submit. The use_count threshold is 2 instead of 1 to account
  // for the additional reference held by pendingSignalSemaphore. The event will remain valid until
  // the semaphore is destroyed.
  return semaphore->getBackendSemaphore();
}

std::shared_ptr<MetalResource> MetalGPU::addResource(MetalResource* resource) {
  DEBUG_ASSERT(resource != nullptr);
  resources.push_back(resource);
  resource->cachedPosition = --resources.end();
  return std::static_pointer_cast<MetalResource>(returnQueue->makeShared(resource));
}

void MetalGPU::processUnreferencedResources() {
  DEBUG_ASSERT(returnQueue != nullptr);
  while (auto resource = static_cast<MetalResource*>(returnQueue->dequeue())) {
    resources.erase(resource->cachedPosition);
    resource->onRelease(this);
    delete resource;
  }
}

void MetalGPU::releaseAll(bool releaseGPU) {
  if (releaseGPU) {
    for (auto& resource : resources) {
      resource->onRelease(this);
    }
  }
  resources.clear();
  returnQueue = nullptr;
}

}  // namespace tgfx