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

#include <webgpu/webgpu.h>
#include <list>
#include <memory>
#include <unordered_map>
#include "WebGPUCaps.h"
#include "core/utils/ReturnQueue.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

class WebGPUCommandQueue;
class WebGPUResource;

/**
 * WebGPU GPU implementation.
 */
class WebGPUGPU : public GPU {
 public:
  static std::unique_ptr<WebGPUGPU> Make(WGPUDevice device);

  ~WebGPUGPU();

  WGPUDevice device() const {
    return webgpuDevice;
  }

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

  WGPUTextureFormat getWGPUTextureFormat(PixelFormat format) const {
    return caps->getWGPUTextureFormat(format);
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

  template <typename T, typename... Args>
  std::shared_ptr<T> makeResource(Args&&... args) {
    static_assert(std::is_base_of_v<WebGPUResource, T>, "T must be a subclass of WebGPUResource!");
    auto resource = new T(std::forward<Args>(args)...);
    return std::static_pointer_cast<T>(addResource(resource));
  }

  void processUnreferencedResources();

  void releaseAll(bool releaseGPU);

  struct MipmapPipeline {
    WGPUShaderModule shaderModule = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    WGPUSampler sampler = nullptr;
  };

  const MipmapPipeline* getMipmapPipeline(WGPUTextureFormat format);

 private:
  explicit WebGPUGPU(WGPUDevice device);

  std::shared_ptr<WebGPUResource> addResource(WebGPUResource* resource);

  static uint32_t MakeSamplerKey(const SamplerDescriptor& descriptor);

  WGPUDevice webgpuDevice = nullptr;
  std::unique_ptr<WebGPUCaps> caps = nullptr;
  std::unique_ptr<WebGPUCommandQueue> commandQueue = nullptr;
  std::list<WebGPUResource*> resources = {};
  std::shared_ptr<ReturnQueue> returnQueue = ReturnQueue::Make();
  std::unordered_map<uint32_t, std::shared_ptr<Sampler>> samplerCache = {};
  std::unordered_map<uint32_t, MipmapPipeline> mipmapPipelineCache = {};
};

}  // namespace tgfx
