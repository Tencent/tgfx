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
#include <unordered_map>
#include <vector>
#include "core/utils/ReturnQueue.h"
#include "gpu/vulkan/VulkanAPI.h"
#include "gpu/vulkan/VulkanCaps.h"
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
 *   waitAllInflightSubmissions -> commandQueue -> fencePool -> caches -> resources -> VMA -> Device
 */
class VulkanGPU : public GPU {
 public:
  static std::unique_ptr<VulkanGPU> Make();

  static std::unique_ptr<VulkanGPU> MakeFrom(VkInstance instance, VkPhysicalDevice physicalDevice,
                                             VkDevice device, VkQueue queue,
                                             uint32_t queueFamilyIndex);

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

  VkDescriptorPool acquireDescriptorPool();

  void releaseDescriptorPool(VkDescriptorPool pool);

  /// Destroys all Vulkan objects in a FrameSession that was never submitted to the GPU. Called
  /// when a CommandBuffer or CommandEncoder is abandoned (destroyed without reaching submit()).
  /// Safe to call because the GPU never executed commands referencing these resources.
  void reclaimAbandonedSession(FrameSession session);

  /// Returns the time point at which the most recently completed GPU submission finished, as
  /// determined by the corresponding fence signal. Used by ResourceCache to determine when a cached
  /// resource can be safely reused (its lastUsedTime must be <= lastFenceSignalTime).
  std::chrono::steady_clock::time_point lastFenceSignalTime() const;

  /// Snapshot of a single writeTexture() call. The staging buffer holds a CPU-side copy of the
  /// pixel data; the actual GPU copy is recorded at submit() time. After the fence signals, the
  /// staging buffer is destroyed via reclaimSubmission().
  struct PendingUpload {
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    std::shared_ptr<VulkanTexture> texture;
    VkBufferImageCopy region = {};
    /// The layout the texture had before writeTexture() was called. Used as oldLayout in the
    /// pre-copy barrier during flushPendingUploads().
    VkImageLayout originalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  };

  /// All parameters needed for a single vkQueueSubmit + optional vkQueuePresentKHR. Assembled by
  /// VulkanCommandQueue::submit() and consumed by VulkanGPU::executeSubmission(). After consumption,
  /// all move-only fields (session, uploads, semaphores) have been transferred to InflightSubmission.
  struct SubmitRequest {
    /// Per-frame resources collected during encoding (command pool, descriptor pool, framebuffers,
    /// render passes, retained resource references). Moved into InflightSubmission on success.
    FrameSession session;
    /// Staging buffers from writeTexture() calls since the last submit. Their GPU copy commands
    /// are already recorded in commandBuffers. Moved into InflightSubmission for deferred cleanup.
    std::vector<PendingUpload> uploads;
    /// Ordered list of command buffers to submit: [uploadCmd?, renderCmd, presentCmd?].
    std::vector<VkCommandBuffer> commandBuffers;
    /// Timeline semaphore to signal after this submission completes (from insertSemaphore()).
    std::shared_ptr<VulkanSemaphore> signalSemaphore;
    /// Timeline semaphore to wait on before executing this submission (from waitSemaphore()).
    std::shared_ptr<VulkanSemaphore> waitSemaphore;
    /// Snapshot of CommandQueue::_frameTime at the time of submit, stored in InflightSubmission
    /// and used to update lastFenceSignalTime when the fence signals.
    std::chrono::steady_clock::time_point frameTime = {};
    /// If non-null, vkQueuePresentKHR is called after the submit with this swapchain.
    VkSwapchainKHR presentSwapchain = VK_NULL_HANDLE;
    uint32_t presentImageIndex = 0;
  };

  /// Submits a completed frame to the GPU. Performs the following steps in order:
  ///   1. pollCompletedSubmissions() — non-blocking reclaim of finished frames.
  ///   2. Backpressure — if MAX_FRAMES_IN_FLIGHT reached, blocks until oldest fence signals.
  ///   3. Acquires a fence and calls vkQueueSubmit with the provided command buffers.
  ///   4. On success: packs session + uploads into InflightSubmission for deferred reclamation.
  ///   5. On failure: immediately reclaims all resources via reclaimSubmission().
  ///   6. If presentSwapchain is set, calls vkQueuePresentKHR (same-queue ordering guarantees
  ///      the layout transition in the submit batch completes before presentation reads).
  void executeSubmission(SubmitRequest request);

  /// Blocks until all in-flight GPU submissions complete and reclaims their resources. Called by
  /// VulkanCommandQueue::waitUntilCompleted() and during releaseAll() shutdown sequence.
  void waitAllInflightSubmissions();

 private:
  VulkanGPU();

  bool initVulkan();
  bool createInstance();
  bool pickPhysicalDevice();
  bool createDevice();
  bool createAllocator();

  std::shared_ptr<VulkanResource> addResource(VulkanResource* resource);

  static uint32_t MakeSamplerKey(const SamplerDescriptor& descriptor);

  // A single vkQueueSubmit whose GPU completion has not yet been confirmed. Holds all resources
  // that must outlive GPU execution: the FrameSession (encoding resources) and PendingUploads
  // (staging buffers). Created in submit() on success, destroyed in reclaimSubmission() after the
  // fence signals. The fence itself is recycled back into the fencePool for reuse.
  struct InflightSubmission {
    VkFence fence = VK_NULL_HANDLE;
    std::chrono::steady_clock::time_point frameTime = {};
    FrameSession session;
    std::vector<PendingUpload> uploads;
  };

  static constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;

  void reclaimSubmission(InflightSubmission& submission);
  void pollCompletedSubmissions();
  VkFence acquireFence();
  void recycleFence(VkFence fence);

  VkInstance vulkanInstance = VK_NULL_HANDLE;
  VkPhysicalDevice vulkanPhysicalDevice = VK_NULL_HANDLE;
  VkDevice vulkanDevice = VK_NULL_HANDLE;
  VkQueue vulkanQueue = VK_NULL_HANDLE;
  uint32_t queueFamilyIndex = 0;
  VmaAllocator vmaAllocator = VK_NULL_HANDLE;
  bool adopted = false;

  std::unique_ptr<VulkanCaps> caps = nullptr;
  std::unique_ptr<VulkanCommandQueue> commandQueue = nullptr;
  std::unique_ptr<shaderc::Compiler> compiler = nullptr;
  std::list<VulkanResource*> resources = {};
  std::shared_ptr<ReturnQueue> returnQueue = ReturnQueue::Make();
  std::unordered_map<uint32_t, std::shared_ptr<Sampler>> samplerCache = {};
  std::vector<VkDescriptorPool> descriptorPoolCache = {};

  // Frame submission lifecycle state (fence-driven asynchronous reclamation).
  std::deque<InflightSubmission> inflightSubmissions;
  std::vector<VkFence> fencePool;
  std::atomic<int64_t> _lastFenceSignalTime = {0};
};

}  // namespace tgfx
