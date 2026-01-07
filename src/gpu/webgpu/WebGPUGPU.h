/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <unordered_map>
#include "WebGPUCaps.h"
#include "WebGPUCommandQueue.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

class WebGPUCommandQueue;

class WebGPUGPU : public GPU {
 public:
  WebGPUGPU(wgpu::Instance instance, wgpu::Adapter adapter, wgpu::Device device,
            std::shared_ptr<WebGPUCaps> caps);

  wgpu::Instance wgpuInstance() const {
    return _instance;
  }

  wgpu::Adapter wgpuAdapter() const {
    return _adapter;
  }

  wgpu::Device wgpuDevice() const {
    return _device;
  }

  wgpu::Queue wgpuQueue() const {
    return _queue;
  }

  const GPUInfo* info() const override {
    return webGPUCaps->info();
  }

  const GPUFeatures* features() const override {
    return webGPUCaps->features();
  }

  const GPULimits* limits() const override {
    return webGPUCaps->limits();
  }

  CommandQueue* queue() const override {
    return commandQueue.get();
  }

  bool isFormatRenderable(PixelFormat pixelFormat) const override {
    return webGPUCaps->isFormatRenderable(pixelFormat);
  }

  int getSampleCount(int requestedCount, PixelFormat pixelFormat) const override {
    return webGPUCaps->getSampleCount(requestedCount, pixelFormat);
  }

  std::shared_ptr<GPUBuffer> createBuffer(size_t size, uint32_t usage) override;

  std::shared_ptr<Texture> createTexture(const TextureDescriptor& descriptor) override;

  std::shared_ptr<Texture> importBackendTexture(const BackendTexture& backendTexture,
                                                uint32_t usage, bool adopted) override;

  std::vector<std::shared_ptr<Texture>> importHardwareTextures(HardwareBufferRef hardwareBuffer,
                                                               uint32_t usage) override;

  std::shared_ptr<Texture> importBackendRenderTarget(
      const BackendRenderTarget& renderTarget) override;

  // WebGPU uses an implicit synchronization model and does not expose explicit synchronization
  // primitives like semaphores. Always returns nullptr.
  std::shared_ptr<Semaphore> importBackendSemaphore(const BackendSemaphore& semaphore) override;

  // WebGPU uses an implicit synchronization model and does not expose explicit synchronization
  // primitives like semaphores. Always returns an empty BackendSemaphore.
  BackendSemaphore stealBackendSemaphore(std::shared_ptr<Semaphore> semaphore) override;

  std::shared_ptr<Sampler> createSampler(const SamplerDescriptor& descriptor) override;

  std::shared_ptr<ShaderModule> createShaderModule(
      const ShaderModuleDescriptor& descriptor) override;

  std::shared_ptr<RenderPipeline> createRenderPipeline(
      const RenderPipelineDescriptor& descriptor) override;

  std::shared_ptr<CommandEncoder> createCommandEncoder() override;

  wgpu::RenderPipeline getMipmapPipeline(PixelFormat format);

  wgpu::Sampler getMipmapSampler();

  wgpu::BindGroupLayout getMipmapBindGroupLayout();

 private:
  void initMipmapResources();
  wgpu::Instance _instance = nullptr;
  wgpu::Adapter _adapter = nullptr;
  wgpu::Device _device = nullptr;
  wgpu::Queue _queue = nullptr;
  std::shared_ptr<WebGPUCaps> webGPUCaps = nullptr;
  std::unique_ptr<WebGPUCommandQueue> commandQueue = nullptr;

  // Mipmap generation resources
  wgpu::ShaderModule mipmapShaderModule = nullptr;
  wgpu::BindGroupLayout mipmapBindGroupLayout = nullptr;
  wgpu::PipelineLayout mipmapPipelineLayout = nullptr;
  wgpu::Sampler mipmapSampler = nullptr;
  std::unordered_map<PixelFormat, wgpu::RenderPipeline> mipmapPipelines = {};
};
}  // namespace tgfx
