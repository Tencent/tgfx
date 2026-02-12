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

#include "MtlGPU.h"
#include "MtlCommandQueue.h"
#include "MtlCommandEncoder.h"
#include "MtlBuffer.h"
#include "MtlTexture.h"
#include "MtlHardwareTexture.h"
#include "MtlSampler.h"
#include "MtlSemaphore.h"
#include "MtlShaderModule.h"
#include "MtlRenderPipeline.h"
#include "core/utils/Log.h"

namespace tgfx {

bool HardwareBufferAvailable() {
  return true;
}

std::unique_ptr<MtlGPU> MtlGPU::Make(id<MTLDevice> device) {
  if (device == nil) {
    return nullptr;
  }
  
  auto gpu = std::unique_ptr<MtlGPU>(new MtlGPU(device));
  if (!gpu->caps || !gpu->commandQueue) {
    return nullptr;
  }
  
  return gpu;
}

MtlGPU::MtlGPU(id<MTLDevice> device) : mtlDevice([device retain]) {
  // Initialize capabilities
  caps = std::make_unique<MtlCaps>(device);
  
  // Initialize command queue
  commandQueue = std::make_unique<MtlCommandQueue>(this);
}

MtlGPU::~MtlGPU() {
  // The owner must call releaseAll() before deleting this MtlGPU, otherwise, GPU resources
  // may leak.
  DEBUG_ASSERT(returnQueue == nullptr);
  DEBUG_ASSERT(resources.empty());
  if (textureCache != nil) {
    CFRelease(textureCache);
  }
  if (mtlDevice != nil) {
    [mtlDevice release];
    mtlDevice = nil;
  }
}

CommandQueue* MtlGPU::queue() const {
  return commandQueue.get();
}

std::shared_ptr<GPUBuffer> MtlGPU::createBuffer(size_t size, uint32_t usage) {
  if (size == 0) {
    return nullptr;
  }
  
  return MtlBuffer::Make(this, size, usage);
}

std::shared_ptr<Texture> MtlGPU::createTexture(const TextureDescriptor& descriptor) {
  if (descriptor.width <= 0 || descriptor.height <= 0) {
    NSLog(@"MtlGPU::createTexture() invalid dimensions: %dx%d", descriptor.width, descriptor.height);
    return nullptr;
  }
  
  // Validate format support
  if (!isFormatRenderable(descriptor.format) && 
      (descriptor.usage & TextureUsage::RENDER_ATTACHMENT)) {
    NSLog(@"MtlGPU::createTexture() format not renderable for render attachment");
    return nullptr;
  }
  
  auto texture = MtlTexture::Make(this, descriptor);
  if (!texture) {
    NSLog(@"MtlGPU::createTexture() MtlTexture::Make failed for %dx%d format=%d", 
          descriptor.width, descriptor.height, static_cast<int>(descriptor.format));
  }
  return texture;
}

std::shared_ptr<Sampler> MtlGPU::createSampler(const SamplerDescriptor& descriptor) {
  return MtlSampler::Make(this, descriptor);
}

std::shared_ptr<ShaderModule> MtlGPU::createShaderModule(
    const ShaderModuleDescriptor& descriptor) {
  return MtlShaderModule::Make(this, descriptor);
}

std::shared_ptr<RenderPipeline> MtlGPU::createRenderPipeline(
    const RenderPipelineDescriptor& descriptor) {
  return MtlRenderPipeline::Make(this, descriptor);
}

std::shared_ptr<CommandEncoder> MtlGPU::createCommandEncoder() {
  processUnreferencedResources();
  return MtlCommandEncoder::Make(this);
}

int MtlGPU::getSampleCount(int requestedCount, PixelFormat pixelFormat) const {
  return caps->getSampleCount(requestedCount, pixelFormat);
}

std::vector<std::shared_ptr<Texture>> MtlGPU::importHardwareTextures(HardwareBufferRef hardwareBuffer,
                                                                     uint32_t usage) {
  return MtlHardwareTexture::MakeFrom(this, hardwareBuffer, usage, getTextureCache());
}

CVMetalTextureCacheRef MtlGPU::getTextureCache() {
  if (textureCache == nil) {
    CVMetalTextureCacheCreate(kCFAllocatorDefault, nil, mtlDevice, nil, &textureCache);
  }
  return textureCache;
}

std::shared_ptr<Texture> MtlGPU::importBackendTexture(const BackendTexture& backendTexture,
                                                      uint32_t usage, bool adopted) {
  if (backendTexture.backend() != Backend::Metal) {
    return nullptr;
  }
  
  MtlTextureInfo mtlInfo = {};
  if (!backendTexture.getMtlTextureInfo(&mtlInfo) || mtlInfo.texture == nullptr) {
    return nullptr;
  }
  
  id<MTLTexture> mtlTexture = (__bridge id<MTLTexture>)mtlInfo.texture;
  return MtlTexture::MakeFrom(this, mtlTexture, usage, adopted);
}

std::shared_ptr<Texture> MtlGPU::importBackendRenderTarget(
    const BackendRenderTarget& backendRenderTarget) {
  MtlTextureInfo mtlInfo = {};
  if (!backendRenderTarget.getMtlTextureInfo(&mtlInfo)) {
    return nullptr;
  }
  auto format = backendRenderTarget.format();
  if (!isFormatRenderable(format)) {
    return nullptr;
  }
  id<MTLTexture> mtlTexture = (__bridge id<MTLTexture>)mtlInfo.texture;
  if (!mtlTexture) {
    return nullptr;
  }
  return MtlTexture::MakeFrom(this, mtlTexture, TextureUsage::RENDER_ATTACHMENT, false);
}

std::shared_ptr<Semaphore> MtlGPU::importBackendSemaphore(const BackendSemaphore& semaphore) {
  if (semaphore.backend() != Backend::Metal) {
    return nullptr;
  }
  
  MtlSemaphoreInfo mtlInfo = {};
  if (!semaphore.getMtlSemaphore(&mtlInfo) || mtlInfo.event == nullptr) {
    return nullptr;
  }
  
  id<MTLEvent> event = (__bridge id<MTLEvent>)mtlInfo.event;
  return MtlSemaphore::MakeFrom(this, event, mtlInfo.value);
}

BackendSemaphore MtlGPU::stealBackendSemaphore(std::shared_ptr<Semaphore> semaphore) {
  if (semaphore == nullptr) {
    return {};
  }
  // For Metal, we don't clear the _event because the command queue's pendingSemaphore
  // still needs to signal it during submit. The event will remain valid until
  // the semaphore is destroyed.
  return semaphore->getBackendSemaphore();
}

std::shared_ptr<MtlResource> MtlGPU::addResource(MtlResource* resource) {
  DEBUG_ASSERT(resource != nullptr);
  resources.push_back(resource);
  resource->cachedPosition = --resources.end();
  return std::static_pointer_cast<MtlResource>(returnQueue->makeShared(resource));
}

void MtlGPU::processUnreferencedResources() {
  DEBUG_ASSERT(returnQueue != nullptr);
  while (auto resource = static_cast<MtlResource*>(returnQueue->dequeue())) {
    resources.erase(resource->cachedPosition);
    resource->onRelease(this);
    delete resource;
  }
}

void MtlGPU::releaseAll(bool releaseGPU) {
  if (releaseGPU) {
    for (auto& resource : resources) {
      resource->onRelease(this);
    }
  }
  resources.clear();
  returnQueue = nullptr;
}

}  // namespace tgfx