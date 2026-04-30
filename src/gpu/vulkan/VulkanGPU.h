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

#include <list>
#include <memory>
#include <unordered_map>
#include "core/utils/ReturnQueue.h"
#include "gpu/vulkan/VulkanAPI.h"
#include "gpu/vulkan/VulkanCaps.h"
#include "tgfx/gpu/GPU.h"

VK_DEFINE_HANDLE(VmaAllocator)

namespace shaderc {
class Compiler;
}

namespace tgfx {

class VulkanCommandQueue;
class VulkanResource;

/**
 * Vulkan GPU implementation.
 */
class VulkanGPU : public GPU {
 public:
  static std::unique_ptr<VulkanGPU> Make();

  ~VulkanGPU() override;

  VkInstance instance() const {
    return vulkanInstance;
  }

  VkPhysicalDevice physicalDevice() const {
    return vulkanPhysicalDevice;
  }

  VkDevice device() const {
    return vulkanDevice;
  }

  VkQueue graphicsQueue() const {
    return vulkanQueue;
  }

  uint32_t graphicsQueueIndex() const {
    return queueFamilyIndex;
  }

  VmaAllocator allocator() const {
    return vmaAllocator;
  }

  const VulkanCaps* vulkanCaps() const {
    return caps.get();
  }

  const shaderc::Compiler* shaderCompiler() const;

  VkFormat getVkFormat(PixelFormat format) const {
    return caps->getVkFormat(format);
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

  int getSampleCount(int requestedCount, PixelFormat pixelFormat) const override;

  std::shared_ptr<GPUBuffer> createBuffer(size_t size, uint32_t usage) override;

  std::shared_ptr<Texture> createTexture(const TextureDescriptor& descriptor) override;

  std::shared_ptr<Sampler> createSampler(const SamplerDescriptor& descriptor) override;

  std::shared_ptr<ShaderModule> createShaderModule(
      const ShaderModuleDescriptor& descriptor) override;

  std::shared_ptr<RenderPipeline> createRenderPipeline(
      const RenderPipelineDescriptor& descriptor) override;

  std::shared_ptr<CommandEncoder> createCommandEncoder() override;

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
    static_assert(std::is_base_of_v<VulkanResource, T>, "T must be a subclass of VulkanResource!");
    auto resource = new T(std::forward<Args>(args)...);
    return std::static_pointer_cast<T>(addResource(resource));
  }

  void processUnreferencedResources();

  void releaseAll(bool releaseGPU);

 private:
  VulkanGPU();

  bool initVulkan();
  bool createInstance();
  bool pickPhysicalDevice();
  bool createDevice();
  bool createAllocator();

  std::shared_ptr<VulkanResource> addResource(VulkanResource* resource);

  static uint32_t MakeSamplerKey(const SamplerDescriptor& descriptor);

  VkInstance vulkanInstance = VK_NULL_HANDLE;
  VkPhysicalDevice vulkanPhysicalDevice = VK_NULL_HANDLE;
  VkDevice vulkanDevice = VK_NULL_HANDLE;
  VkQueue vulkanQueue = VK_NULL_HANDLE;
  uint32_t queueFamilyIndex = 0;
  VmaAllocator vmaAllocator = VK_NULL_HANDLE;

  std::unique_ptr<VulkanCaps> caps = nullptr;
  std::unique_ptr<VulkanCommandQueue> commandQueue = nullptr;
  std::unique_ptr<shaderc::Compiler> compiler = nullptr;
  std::list<VulkanResource*> resources = {};
  std::shared_ptr<ReturnQueue> returnQueue = ReturnQueue::Make();
  std::unordered_map<uint32_t, std::shared_ptr<Sampler>> samplerCache = {};
};

}  // namespace tgfx
