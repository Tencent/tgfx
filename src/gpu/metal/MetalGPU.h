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

#pragma once

#include <CoreVideo/CoreVideo.h>
#include <Metal/Metal.h>
#include <list>
#include <memory>
#include "tgfx/gpu/GPU.h"
#include "MetalCaps.h"
#include "core/utils/ReturnQueue.h"

namespace tgfx {

class MetalCommandQueue;
class MetalResource;

/**
 * Metal GPU implementation.
 */
class MetalGPU : public GPU {
 public:
  /**
   * Creates a Metal GPU with the given Metal device.
   */
  static std::unique_ptr<MetalGPU> Make(id<MTLDevice> device);

  ~MetalGPU();

  /**
   * Returns the Metal device.
   */
  id<MTLDevice> device() const {
    return metalDevice;
  }

  // GPU interface implementation
  const GPUInfo* info() const override {
    return caps->info();
  }

  const GPUFeatures* features() const override {
    return caps->features();
  }

  const GPULimits* limits() const override {
    return caps->limits();
  }

  CommandQueue* queue() const override;

  bool isFormatRenderable(PixelFormat format) const override {
    return caps->isFormatRenderable(format);
  }

  MTLPixelFormat getMTLPixelFormat(PixelFormat format) const {
    return caps->getMTLPixelFormat(format);
  }

  std::shared_ptr<GPUBuffer> createBuffer(size_t size, uint32_t usage) override;

  std::shared_ptr<Texture> createTexture(const TextureDescriptor& descriptor) override;

  std::shared_ptr<Sampler> createSampler(const SamplerDescriptor& descriptor) override;

  std::shared_ptr<ShaderModule> createShaderModule(
      const ShaderModuleDescriptor& descriptor) override;

  std::shared_ptr<RenderPipeline> createRenderPipeline(
      const RenderPipelineDescriptor& descriptor) override;

  std::shared_ptr<CommandEncoder> createCommandEncoder() override;

  int getSampleCount(int requestedCount, PixelFormat pixelFormat) const override;

  std::vector<std::shared_ptr<Texture>> importHardwareTextures(HardwareBufferRef hardwareBuffer,
                                                               uint32_t usage) override;

  std::shared_ptr<Texture> importBackendTexture(const BackendTexture& backendTexture,
                                                uint32_t usage, bool adopted = false) override;

  std::shared_ptr<Texture> importBackendRenderTarget(
      const BackendRenderTarget& backendRenderTarget) override;

  std::shared_ptr<Semaphore> importBackendSemaphore(const BackendSemaphore& semaphore) override;

  BackendSemaphore stealBackendSemaphore(std::shared_ptr<Semaphore> semaphore) override;

  /**
   * Returns the Metal texture cache for hardware buffer imports.
   */
  CVMetalTextureCacheRef getTextureCache();

  /**
   * Template method to create and manage Metal resources.
   */
  template <typename T, typename... Args>
  std::shared_ptr<T> makeResource(Args&&... args) {
    static_assert(std::is_base_of_v<MetalResource, T>, "T must be a subclass of MetalResource!");
    auto resource = new T(std::forward<Args>(args)...);
    return std::static_pointer_cast<T>(addResource(resource));
  }

  /**
   * Processes unreferenced resources for cleanup.
   */
  void processUnreferencedResources();

  /**
   * Releases all managed resources. If releaseGPU is true, calls onRelease() on each resource
   * before removing it. The caller must ensure that all external shared_ptr references to resources
   * created by this GPU have been released before calling this method.
   */
  void releaseAll(bool releaseGPU);

 private:
  explicit MetalGPU(id<MTLDevice> device);

  std::shared_ptr<MetalResource> addResource(MetalResource* resource);

  id<MTLDevice> metalDevice = nil;
  std::unique_ptr<MetalCaps> caps = nullptr;
  std::unique_ptr<MetalCommandQueue> commandQueue = nullptr;
  std::list<MetalResource*> resources = {};
  std::shared_ptr<ReturnQueue> returnQueue = ReturnQueue::Make();
  CVMetalTextureCacheRef textureCache = nil;
};

}  // namespace tgfx