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

#include <atomic>
#include <chrono>
#include <deque>
#include <list>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include "core/utils/ReturnQueue.h"
#include "gpu/vulkan/VulkanAPI.h"
#include "gpu/vulkan/VulkanCaps.h"
#include "gpu/vulkan/VulkanExtensions.h"
#include "gpu/vulkan/VulkanFrameSession.h"
#include "tgfx/gpu/GPU.h"

VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

namespace shaderc {
class Compiler;
}

namespace tgfx {

class VulkanCommandQueue;
class VulkanSemaphore;
class VulkanTexture;

/**
 * Vulkan GPU implementation. Owns the VkDevice and all device-scoped state.
 *
 * Three responsibilities:
 *   1. Resource factory — creates textures, buffers, pipelines, samplers, encoders.
 *   2. Device access — exposes VkDevice/VkQueue/VmaAllocator to internal Vulkan classes.
 *   3. Frame submission lifecycle — manages the fence pool, in-flight submission queue, and
 *      resource reclamation. This is the single authority that determines when GPU resources
 *      can be safely destroyed (via fence signal).
 *
 * Resource ownership model:
 *   - Long-lived resources (VkBuffer, VkImage, VkPipeline) are wrapped in VulkanResource
 *     subclasses and tracked via shared_ptr + ReturnQueue. When refcount reaches zero, they
 *     enter the ReturnQueue; processUnreferencedResources() calls onRelease() on the GPU thread.
 *   - Per-frame resources (VkCommandPool, VkRenderPass, VkFramebuffer, VkDescriptorPool,
 *     staging buffers) are aggregated in a FrameSession and held by an InflightSubmission.
 *     reclaimSubmission() destroys them after the fence signals.
 *   - The InflightSubmission also holds shared_ptr references (retainedResources) to prevent
 *     long-lived resources from being destroyed while the GPU is still reading them.
 *
 * Shutdown ordering (releaseAll):
 *   waitAllInflightSubmissions -> commandQueue -> fencePool -> presentationSlots -> caches ->
 *   resources -> VMA -> Device
 */
class VulkanGPU : public GPU {
 public:
  static std::unique_ptr<VulkanGPU> Make();

  static std::unique_ptr<VulkanGPU> MakeFrom(VkInstance instance, VkPhysicalDevice physicalDevice,
                                             VkDevice device, VkQueue queue,
                                             uint32_t queueFamilyIndex);

  ~VulkanGPU() override;

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

  // Vulkan handle accessors
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

  VkPipelineCache pipelineCache() const {
    return vulkanPipelineCache;
  }

  const VulkanCaps* vulkanCaps() const {
    return caps.get();
  }

  const VulkanExtensions& extensions() const {
    return _extensions;
  }

  const shaderc::Compiler* shaderCompiler() const;

  VkFormat getVkFormat(PixelFormat format) const {
    return caps->getVkFormat(format);
  }

  // Resource management (used by all internal Vulkan classes)
  // Long-lived resources are tracked via shared_ptr + ReturnQueue. When refcount reaches zero,
  // processUnreferencedResources() calls onRelease() on the GPU thread.
  template <typename T, typename... Args>
  std::shared_ptr<T> makeResource(Args&&... args) {
    static_assert(std::is_base_of_v<VulkanResource, T>, "T must be a subclass of VulkanResource!");
    auto resource = new T(std::forward<Args>(args)...);
    return std::static_pointer_cast<T>(addResource(resource));
  }

  void processUnreferencedResources();
  void releaseAll(bool releaseGPU);

  // Submission (used by VulkanCommandQueue)
  // VulkanCommandQueue assembles a SubmitRequest from pending uploads, render commands, and
  // semaphores, then hands it to executeSubmission(). The GPU manages fence allocation,
  // backpressure, vkQueueSubmit, inflight tracking, and resource reclamation after fence signal.
  static constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;

  // Snapshot of a writeTexture() call. Staging buffer holds CPU-side pixel data; the GPU copy
  // is recorded at submit() time. Destroyed via reclaimSubmission() after fence signals.
  struct PendingUpload {
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    std::shared_ptr<VulkanTexture> texture;
    VkBufferImageCopy region = {};
    VkImageLayout originalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  };

  // All parameters for a single vkQueueSubmit + optional vkQueuePresentKHR. After consumption,
  // move-only fields (session, uploads, semaphores) are transferred into InflightSubmission.
  struct SubmitRequest {
    FrameSession session;
    std::vector<PendingUpload> uploads;
    std::vector<VkCommandBuffer> commandBuffers;
    std::shared_ptr<VulkanSemaphore> signalSemaphore;
    std::shared_ptr<VulkanSemaphore> waitSemaphore;
    std::chrono::steady_clock::time_point frameTime = {};

    struct PresentInfo {
      VkSwapchainKHR swapchain = VK_NULL_HANDLE;
      uint32_t imageIndex = 0;
      VkSemaphore imageAvailable = VK_NULL_HANDLE;
      VkSemaphore renderFinished = VK_NULL_HANDLE;
    };
    std::optional<PresentInfo> present;
  };

  void executeSubmission(SubmitRequest request);
  void waitAllInflightSubmissions();
  std::chrono::steady_clock::time_point lastFenceSignalTime() const;
  void reclaimAbandonedSession(FrameSession session);
  VkDescriptorPool acquireDescriptorPool();
  void releaseDescriptorPool(VkDescriptorPool pool);

  // Presentation (used by VulkanWindow/SwapchainProxy)
  // Binary semaphore pairs for swapchain acquire/present sync. Indexed per frame-in-flight to
  // prevent reuse while still in flight (VUID-vkAcquireNextImageKHR-semaphore-01779).
  // acquirePresentationSlot() blocks if needed to ensure the returned pair is safe.
  struct PresentationSlot {
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
  };

  const PresentationSlot& acquirePresentationSlot();

 private:
  VulkanGPU();

  // Initialization
  bool initVulkan();
  bool createInstance();
  void installDebugMessenger();
  bool pickPhysicalDevice();
  bool createDevice();
  bool createAllocator();
  bool createPresentationSlots();

  // Device handles (immutable after init; when adopted==true, caller owns device and instance)
  VkInstance vulkanInstance = VK_NULL_HANDLE;
  VkPhysicalDevice vulkanPhysicalDevice = VK_NULL_HANDLE;
  VkDevice vulkanDevice = VK_NULL_HANDLE;
  VkQueue vulkanQueue = VK_NULL_HANDLE;
  uint32_t queueFamilyIndex = 0;
  VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
  bool debugUtilsEnabled = false;
  VmaAllocator vmaAllocator = VK_NULL_HANDLE;
  VkPipelineCache vulkanPipelineCache = VK_NULL_HANDLE;
  VulkanExtensions _extensions;
  bool adopted = false;

  // Subsystems
  std::unique_ptr<VulkanCaps> caps = nullptr;
  std::unique_ptr<VulkanCommandQueue> commandQueue = nullptr;
  std::unique_ptr<shaderc::Compiler> compiler = nullptr;

  // Resource tracking (resources list + ReturnQueue for deferred destruction)
  std::shared_ptr<VulkanResource> addResource(VulkanResource* resource);
  static uint32_t MakeSamplerKey(const SamplerDescriptor& descriptor);

  std::list<VulkanResource*> resources = {};
  std::shared_ptr<ReturnQueue> returnQueue = ReturnQueue::Make();
  std::unordered_map<uint32_t, std::shared_ptr<Sampler>> samplerCache = {};
  std::vector<VkDescriptorPool> descriptorPoolCache = {};

  // Submission state (fence pool, inflight queue, presentation slots)
  // InflightSubmission holds per-frame resources until the fence confirms GPU completion.
  struct InflightSubmission {
    VkFence fence = VK_NULL_HANDLE;
    std::chrono::steady_clock::time_point frameTime = {};
    FrameSession session;
    std::vector<PendingUpload> uploads;
  };

  void reclaimSubmission(InflightSubmission& submission);
  void pollCompletedSubmissions();
  VkFence acquireFence();
  void recycleFence(VkFence fence);

  std::deque<InflightSubmission> inflightSubmissions;
  std::vector<VkFence> fencePool;
  std::atomic<int64_t> _lastFenceSignalTime = {0};

  PresentationSlot presentationSlots[MAX_FRAMES_IN_FLIGHT] = {};
  uint32_t presentationSlotIndex = 0;
  bool presentationSlotsCreated = false;

  bool contextLost = false;
};

}  // namespace tgfx
