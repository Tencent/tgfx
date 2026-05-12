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

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include "VulkanGPU.h"
#include <cstring>
#include <shaderc/shaderc.hpp>
#include <vector>
#include "core/utils/Log.h"
#include "gpu/vulkan/VulkanBuffer.h"
#include "gpu/vulkan/VulkanCommandEncoder.h"
#include "gpu/vulkan/VulkanCommandQueue.h"
#include "gpu/vulkan/VulkanRenderPipeline.h"
#include "gpu/vulkan/VulkanResource.h"
#include "gpu/vulkan/VulkanSampler.h"
#include "gpu/vulkan/VulkanSemaphore.h"
#include "gpu/vulkan/VulkanShaderModule.h"
#include "gpu/vulkan/VulkanTexture.h"
#include "gpu/vulkan/VulkanUtil.h"

namespace tgfx {

bool HardwareBufferAvailable() {
  return false;
}

std::unique_ptr<VulkanGPU> VulkanGPU::Make() {
  auto gpu = std::unique_ptr<VulkanGPU>(new VulkanGPU());
  if (!gpu->initVulkan()) {
    return nullptr;
  }
  return gpu;
}

std::unique_ptr<VulkanGPU> VulkanGPU::MakeFrom(VkInstance instance, VkPhysicalDevice physicalDevice,
                                               VkDevice device, VkQueue queue,
                                               uint32_t queueFamilyIndex) {
  if (instance == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE ||
      queue == VK_NULL_HANDLE) {
    LOGE("VulkanGPU::MakeFrom() received null handle.");
    return nullptr;
  }
  if (volkInitialize() != VK_SUCCESS) {
    LOGE("VulkanGPU::MakeFrom() volkInitialize failed.");
    return nullptr;
  }
  auto gpu = std::unique_ptr<VulkanGPU>(new VulkanGPU());
  gpu->vulkanInstance = instance;
  gpu->vulkanPhysicalDevice = physicalDevice;
  gpu->vulkanDevice = device;
  gpu->vulkanQueue = queue;
  gpu->queueFamilyIndex = queueFamilyIndex;
  gpu->adopted = true;
  volkLoadInstance(instance);
  volkLoadDevice(device);
  if (!gpu->createAllocator()) {
    return nullptr;
  }
  gpu->caps = std::make_unique<VulkanCaps>(physicalDevice);
  gpu->commandQueue = std::make_unique<VulkanCommandQueue>(gpu.get());
  gpu->compiler = std::make_unique<shaderc::Compiler>();
  return gpu;
}

VulkanGPU::VulkanGPU() {
}

VulkanGPU::~VulkanGPU() {
  DEBUG_ASSERT(returnQueue == nullptr);
  DEBUG_ASSERT(resources.empty());
}

bool VulkanGPU::initVulkan() {
  if (volkInitialize() != VK_SUCCESS) {
    LOGE("VulkanGPU::initVulkan() volkInitialize failed, no Vulkan loader available.");
    return false;
  }
  if (!createInstance()) {
    return false;
  }
  volkLoadInstance(vulkanInstance);
  if (!pickPhysicalDevice()) {
    return false;
  }
  if (!createDevice()) {
    return false;
  }
  volkLoadDevice(vulkanDevice);
  if (!createAllocator()) {
    return false;
  }
  caps = std::make_unique<VulkanCaps>(vulkanPhysicalDevice);
  commandQueue = std::make_unique<VulkanCommandQueue>(this);
  compiler = std::make_unique<shaderc::Compiler>();
  return true;
}

bool VulkanGPU::createInstance() {
  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "TGFX";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "TGFX";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;

  std::vector<const char*> instanceExtensions = {
      VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
      VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined(__ANDROID__)
      VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#endif
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
  };

  VkInstanceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
  createInfo.ppEnabledExtensionNames = instanceExtensions.data();

  auto result = vkCreateInstance(&createInfo, nullptr, &vulkanInstance);
  if (result != VK_SUCCESS) {
    LOGE("VulkanGPU::createInstance() vkCreateInstance failed: %s", VkResultToString(result));
    return false;
  }
  return true;
}

bool VulkanGPU::pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(vulkanInstance, &deviceCount, nullptr);
  if (deviceCount == 0) {
    LOGE("VulkanGPU::pickPhysicalDevice() no Vulkan-capable GPU found.");
    return false;
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(vulkanInstance, &deviceCount, devices.data());

  // Prefer discrete GPU, then integrated, then any.
  VkPhysicalDevice discreteDevice = VK_NULL_HANDLE;
  VkPhysicalDevice integratedDevice = VK_NULL_HANDLE;
  for (auto& dev : devices) {
    VkPhysicalDeviceProperties props = {};
    vkGetPhysicalDeviceProperties(dev, &props);
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
        discreteDevice == VK_NULL_HANDLE) {
      discreteDevice = dev;
    } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
               integratedDevice == VK_NULL_HANDLE) {
      integratedDevice = dev;
    }
  }

  if (discreteDevice != VK_NULL_HANDLE) {
    vulkanPhysicalDevice = discreteDevice;
  } else if (integratedDevice != VK_NULL_HANDLE) {
    vulkanPhysicalDevice = integratedDevice;
  } else {
    vulkanPhysicalDevice = devices[0];
  }
  return true;
}

bool VulkanGPU::createDevice() {
  // Find a graphics queue family.
  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(vulkanPhysicalDevice, &queueFamilyCount, nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(vulkanPhysicalDevice, &queueFamilyCount,
                                           queueFamilies.data());

  bool found = false;
  for (uint32_t i = 0; i < queueFamilyCount; i++) {
    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      queueFamilyIndex = i;
      found = true;
      break;
    }
  }
  if (!found) {
    LOGE("VulkanGPU::createDevice() no graphics queue family found.");
    return false;
  }

  float queuePriority = 1.0f;
  VkDeviceQueueCreateInfo queueCreateInfo = {};
  queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
  queueCreateInfo.queueCount = 1;
  queueCreateInfo.pQueuePriorities = &queuePriority;

  std::vector<const char*> deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  };

  // Enable timeline semaphore if available.
  uint32_t extCount = 0;
  vkEnumerateDeviceExtensionProperties(vulkanPhysicalDevice, nullptr, &extCount, nullptr);
  std::vector<VkExtensionProperties> availableExtensions(extCount);
  vkEnumerateDeviceExtensionProperties(vulkanPhysicalDevice, nullptr, &extCount,
                                       availableExtensions.data());
  bool hasTimelineSemaphore = false;
  bool hasExtendedDynamicState = false;
  for (const auto& ext : availableExtensions) {
    if (strcmp(ext.extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0) {
      hasTimelineSemaphore = true;
    }
    if (strcmp(ext.extensionName, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME) == 0) {
      hasExtendedDynamicState = true;
    }
  }

  // Query actual feature support. An extension being available does not guarantee its feature
  // flag is TRUE (spec allows extension present + feature FALSE).
  VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures = {};
  timelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;

  VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynStateFeatures = {};
  extDynStateFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;

  VkPhysicalDeviceFeatures2 queryFeatures = {};
  queryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  if (hasTimelineSemaphore) {
    timelineFeatures.pNext = hasExtendedDynamicState ? &extDynStateFeatures : nullptr;
    queryFeatures.pNext = &timelineFeatures;
  } else if (hasExtendedDynamicState) {
    queryFeatures.pNext = &extDynStateFeatures;
  }
  vkGetPhysicalDeviceFeatures2(vulkanPhysicalDevice, &queryFeatures);

  // Only enable extensions whose features are confirmed by the physical device.
  hasTimelineSemaphore = hasTimelineSemaphore && timelineFeatures.timelineSemaphore;
  hasExtendedDynamicState = hasExtendedDynamicState && extDynStateFeatures.extendedDynamicState;

  if (hasTimelineSemaphore) {
    deviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
  }
  if (hasExtendedDynamicState) {
    deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
  }

  // Build the feature chain for vkCreateDevice with only verified features.
  VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
  deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  if (hasTimelineSemaphore) {
    timelineFeatures.pNext = hasExtendedDynamicState ? &extDynStateFeatures : nullptr;
    deviceFeatures2.pNext = &timelineFeatures;
  } else if (hasExtendedDynamicState) {
    deviceFeatures2.pNext = &extDynStateFeatures;
  }

  VkDeviceCreateInfo deviceCreateInfo = {};
  deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCreateInfo.pNext = &deviceFeatures2;
  deviceCreateInfo.queueCreateInfoCount = 1;
  deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
  deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
  deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

  auto result = vkCreateDevice(vulkanPhysicalDevice, &deviceCreateInfo, nullptr, &vulkanDevice);
  if (result != VK_SUCCESS) {
    LOGE("VulkanGPU::createDevice() vkCreateDevice failed: %s", VkResultToString(result));
    return false;
  }

  vkGetDeviceQueue(vulkanDevice, queueFamilyIndex, 0, &vulkanQueue);
  return true;
}

bool VulkanGPU::createAllocator() {
  VmaVulkanFunctions vulkanFunctions = {};
  vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
  vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo allocatorInfo = {};
  allocatorInfo.physicalDevice = vulkanPhysicalDevice;
  allocatorInfo.device = vulkanDevice;
  allocatorInfo.instance = vulkanInstance;
  allocatorInfo.pVulkanFunctions = &vulkanFunctions;
  allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;

  auto result = vmaCreateAllocator(&allocatorInfo, &vmaAllocator);
  if (result != VK_SUCCESS) {
    LOGE("VulkanGPU::createAllocator() vmaCreateAllocator failed: %s", VkResultToString(result));
    return false;
  }
  return true;
}

const shaderc::Compiler* VulkanGPU::shaderCompiler() const {
  return compiler.get();
}

CommandQueue* VulkanGPU::queue() const {
  return commandQueue.get();
}

int VulkanGPU::getSampleCount(int requestedCount, PixelFormat pixelFormat) const {
  return caps->getSampleCount(requestedCount, pixelFormat);
}

std::shared_ptr<GPUBuffer> VulkanGPU::createBuffer(size_t size, uint32_t usage) {
  if (size == 0) {
    return nullptr;
  }
  return VulkanBuffer::Make(this, size, usage);
}

std::shared_ptr<Texture> VulkanGPU::createTexture(const TextureDescriptor& descriptor) {
  if (descriptor.width <= 0 || descriptor.height <= 0) {
    LOGE("VulkanGPU::createTexture() invalid dimensions: %dx%d", descriptor.width,
         descriptor.height);
    return nullptr;
  }
  if (!isFormatRenderable(descriptor.format) &&
      (descriptor.usage & TextureUsage::RENDER_ATTACHMENT)) {
    LOGE("VulkanGPU::createTexture() format not renderable for render attachment");
    return nullptr;
  }
  return VulkanTexture::Make(this, descriptor);
}

std::shared_ptr<Sampler> VulkanGPU::createSampler(const SamplerDescriptor& descriptor) {
  auto key = MakeSamplerKey(descriptor);
  auto iter = samplerCache.find(key);
  if (iter != samplerCache.end()) {
    return iter->second;
  }
  auto sampler = VulkanSampler::Make(this, descriptor);
  if (sampler != nullptr) {
    samplerCache[key] = sampler;
  }
  return sampler;
}

std::shared_ptr<ShaderModule> VulkanGPU::createShaderModule(
    const ShaderModuleDescriptor& descriptor) {
  return VulkanShaderModule::Make(this, descriptor);
}

std::shared_ptr<RenderPipeline> VulkanGPU::createRenderPipeline(
    const RenderPipelineDescriptor& descriptor) {
  return VulkanRenderPipeline::Make(this, descriptor);
}

std::shared_ptr<CommandEncoder> VulkanGPU::createCommandEncoder() {
  processUnreferencedResources();
  return VulkanCommandEncoder::Make(this);
}

std::vector<std::shared_ptr<Texture>> VulkanGPU::importHardwareTextures(HardwareBufferRef,
                                                                        uint32_t) {
  return {};
}

std::shared_ptr<Texture> VulkanGPU::importBackendTexture(const BackendTexture& backendTexture,
                                                         uint32_t usage, bool adopted) {
  if (backendTexture.backend() != Backend::Vulkan) {
    return nullptr;
  }
  VulkanTextureInfo vulkanInfo = {};
  if (!backendTexture.getVulkanTextureInfo(&vulkanInfo) || vulkanInfo.image == 0) {
    return nullptr;
  }
  return VulkanTexture::MakeFrom(this, reinterpret_cast<VkImage>(vulkanInfo.image),
                                 static_cast<VkFormat>(vulkanInfo.format), backendTexture.width(),
                                 backendTexture.height(), usage, adopted,
                                 static_cast<VkImageLayout>(vulkanInfo.layout));
}

std::shared_ptr<Texture> VulkanGPU::importBackendRenderTarget(
    const BackendRenderTarget& backendRenderTarget) {
  VulkanImageInfo vulkanInfo = {};
  if (!backendRenderTarget.getVulkanImageInfo(&vulkanInfo)) {
    return nullptr;
  }
  auto format = backendRenderTarget.format();
  if (!isFormatRenderable(format)) {
    return nullptr;
  }
  return VulkanTexture::MakeFrom(this, reinterpret_cast<VkImage>(vulkanInfo.image),
                                 static_cast<VkFormat>(vulkanInfo.format),
                                 backendRenderTarget.width(), backendRenderTarget.height(),
                                 TextureUsage::RENDER_ATTACHMENT, false,
                                 static_cast<VkImageLayout>(vulkanInfo.layout));
}

std::shared_ptr<Semaphore> VulkanGPU::importBackendSemaphore(const BackendSemaphore& semaphore) {
  if (semaphore.backend() != Backend::Vulkan) {
    return nullptr;
  }
  VulkanSyncInfo vulkanInfo = {};
  if (!semaphore.getVulkanSync(&vulkanInfo) || vulkanInfo.semaphore == 0) {
    return nullptr;
  }
  return VulkanSemaphore::MakeFrom(this, reinterpret_cast<VkSemaphore>(vulkanInfo.semaphore),
                                   vulkanInfo.value);
}

BackendSemaphore VulkanGPU::stealBackendSemaphore(std::shared_ptr<Semaphore> semaphore) {
  if (semaphore == nullptr || semaphore.use_count() > 2) {
    return {};
  }
  return semaphore->getBackendSemaphore();
}

uint32_t VulkanGPU::MakeSamplerKey(const SamplerDescriptor& descriptor) {
  uint32_t key = 0;
  key |= static_cast<uint32_t>(descriptor.addressModeX);       // bits 0-2
  key |= static_cast<uint32_t>(descriptor.addressModeY) << 3;  // bits 3-5
  key |= static_cast<uint32_t>(descriptor.minFilter) << 6;     // bits 6-7
  key |= static_cast<uint32_t>(descriptor.magFilter) << 8;     // bits 8-9
  key |= static_cast<uint32_t>(descriptor.mipmapMode) << 10;   // bits 10-11
  return key;
}

std::shared_ptr<VulkanResource> VulkanGPU::addResource(VulkanResource* resource) {
  DEBUG_ASSERT(resource != nullptr);
  resources.push_back(resource);
  resource->cachedPosition = --resources.end();
  return std::static_pointer_cast<VulkanResource>(returnQueue->makeShared(resource));
}

void VulkanGPU::processUnreferencedResources() {
  DEBUG_ASSERT(returnQueue != nullptr);
  while (auto resource = static_cast<VulkanResource*>(returnQueue->dequeue())) {
    resources.erase(resource->cachedPosition);
    resource->onRelease(this);
    delete resource;
  }
}

// Per-pool capacity for descriptor set allocation. Pools are chained on demand when exhausted
// (see VulkanCommandEncoder::allocateDescriptorSet), so this value controls granularity rather
// than a hard per-frame limit. 1024 covers most 2D frames in a single pool; complex scenes
// transparently chain additional pools as needed.
static constexpr uint32_t POOL_MAX_DESCRIPTOR_SETS = 1024;
static constexpr uint32_t POOL_MAX_UNIFORM_BUFFERS = 2048;
static constexpr uint32_t POOL_MAX_COMBINED_SAMPLERS = 1024;

VkDescriptorPool VulkanGPU::acquireDescriptorPool() {
  if (!descriptorPoolCache.empty()) {
    auto pool = descriptorPoolCache.back();
    descriptorPoolCache.pop_back();
    return pool;
  }
  VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, POOL_MAX_UNIFORM_BUFFERS},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, POOL_MAX_COMBINED_SAMPLERS},
  };
  VkDescriptorPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.maxSets = POOL_MAX_DESCRIPTOR_SETS;
  poolInfo.poolSizeCount = 2;
  poolInfo.pPoolSizes = poolSizes;
  VkDescriptorPool pool = VK_NULL_HANDLE;
  auto result = vkCreateDescriptorPool(vulkanDevice, &poolInfo, nullptr, &pool);
  if (result != VK_SUCCESS) {
    LOGE("VulkanGPU::acquireDescriptorPool() vkCreateDescriptorPool failed.");
    return VK_NULL_HANDLE;
  }
  return pool;
}

void VulkanGPU::releaseDescriptorPool(VkDescriptorPool pool) {
  if (pool == VK_NULL_HANDLE) {
    return;
  }
  vkResetDescriptorPool(vulkanDevice, pool, 0);
  descriptorPoolCache.push_back(pool);
}

std::chrono::steady_clock::time_point VulkanGPU::lastFenceSignalTime() const {
  auto ticks = _lastFenceSignalTime.load(std::memory_order_acquire);
  return std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(ticks));
}

VkFence VulkanGPU::acquireFence() {
  if (!fencePool.empty()) {
    auto fence = fencePool.back();
    fencePool.pop_back();
    vkResetFences(vulkanDevice, 1, &fence);
    return fence;
  }
  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence = VK_NULL_HANDLE;
  auto result = vkCreateFence(vulkanDevice, &fenceInfo, nullptr, &fence);
  if (result != VK_SUCCESS) {
    LOGE("VulkanGPU::acquireFence: vkCreateFence failed: %s", VkResultToString(result));
    return VK_NULL_HANDLE;
  }
  return fence;
}

void VulkanGPU::recycleFence(VkFence fence) {
  if (fence == VK_NULL_HANDLE) {
    return;
  }
  if (fencePool.size() >= MAX_FRAMES_IN_FLIGHT + 1) {
    vkDestroyFence(vulkanDevice, fence, nullptr);
    return;
  }
  fencePool.push_back(fence);
}

void VulkanGPU::reclaimAbandonedSession(FrameSession session) {
  // Reuse reclaimSubmission with a dummy InflightSubmission (no fence, no uploads).
  // This is the same cleanup path as submit failure and fence-signal reclamation.
  InflightSubmission abandoned = {};
  abandoned.session = std::move(session);
  reclaimSubmission(abandoned);
}

void VulkanGPU::reclaimSubmission(InflightSubmission& submission) {
  auto& s = submission.session;
  // Staging buffers are owned by the queue (not the encoder), so they are stored in uploads
  // rather than in the FrameSession. Destroyed here after the fence confirms GPU completion.
  for (auto& upload : submission.uploads) {
    vmaDestroyBuffer(vmaAllocator, upload.stagingBuffer, upload.stagingAlloc);
  }
  // Command pool destruction implicitly frees all command buffers allocated from it.
  if (s.commandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(vulkanDevice, s.commandPool, nullptr);
  }
  for (auto fb : s.deferredFramebuffers) {
    vkDestroyFramebuffer(vulkanDevice, fb, nullptr);
  }
  for (auto rp : s.deferredRenderPasses) {
    vkDestroyRenderPass(vulkanDevice, rp, nullptr);
  }
  // Descriptor pools are reset and recycled (not destroyed) to avoid allocation overhead.
  for (auto pool : s.descriptorPools) {
    if (pool != VK_NULL_HANDLE) {
      releaseDescriptorPool(pool);
    }
  }
  // Clearing retainedResources decrements refcounts. Resources reaching zero will enter the
  // ReturnQueue and be destroyed during the next processUnreferencedResources() call.
  s.retainedResources.clear();
  recycleFence(submission.fence);
}

void VulkanGPU::pollCompletedSubmissions() {
  while (!inflightSubmissions.empty()) {
    auto& oldest = inflightSubmissions.front();
    auto status = vkGetFenceStatus(vulkanDevice, oldest.fence);
    if (status == VK_SUCCESS) {
      auto ticks = oldest.frameTime.time_since_epoch().count();
      _lastFenceSignalTime.store(ticks, std::memory_order_release);
      reclaimSubmission(oldest);
      inflightSubmissions.pop_front();
      continue;
    }
    if (status == VK_NOT_READY) {
      // Fence not yet signaled — stop polling; remaining submissions are still in flight.
      break;
    }
    // VK_ERROR_DEVICE_LOST or other fatal error: the fence will never signal. Force-reclaim all
    // remaining inflight submissions to avoid resource leaks and prevent infinite blocking in
    // waitAllInflightSubmissions(). After this, lockContext() will return nullptr (contextLost).
    LOGE("VulkanGPU::pollCompletedSubmissions: vkGetFenceStatus returned %d, reclaiming all "
         "inflight submissions.",
         static_cast<int>(status));
    while (!inflightSubmissions.empty()) {
      reclaimSubmission(inflightSubmissions.front());
      inflightSubmissions.pop_front();
    }
    contextLost = true;
    break;
  }
  processUnreferencedResources();
}

void VulkanGPU::waitAllInflightSubmissions() {
  for (auto& submission : inflightSubmissions) {
    auto result = vkWaitForFences(vulkanDevice, 1, &submission.fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
      // VK_ERROR_DEVICE_LOST or timeout: fence will never signal. Log and force-reclaim.
      LOGE("VulkanGPU::waitAllInflightSubmissions: vkWaitForFences returned %d, force-reclaiming.",
           static_cast<int>(result));
      contextLost = true;
    }
    auto ticks = submission.frameTime.time_since_epoch().count();
    _lastFenceSignalTime.store(ticks, std::memory_order_release);
    reclaimSubmission(submission);
  }
  inflightSubmissions.clear();
  processUnreferencedResources();
}

void VulkanGPU::executeSubmission(SubmitRequest request) {
  // Early out if a previous fatal error (e.g. DEVICE_LOST) was detected. Reclaim resources
  // immediately to prevent leaks; all subsequent Vulkan calls would fail anyway.
  if (contextLost) {
    reclaimAbandonedSession(std::move(request.session));
    for (auto& upload : request.uploads) {
      vmaDestroyBuffer(vmaAllocator, upload.stagingBuffer, upload.stagingAlloc);
    }
    return;
  }

  // Step 1: Non-blocking reclaim of any submissions whose fences have already signaled.
  pollCompletedSubmissions();

  // Step 2: Backpressure — if we've reached MAX_FRAMES_IN_FLIGHT, block until the oldest
  // submission completes. This bounds memory usage when CPU outpaces GPU.
  if (inflightSubmissions.size() >= MAX_FRAMES_IN_FLIGHT) {
    auto& oldest = inflightSubmissions.front();
    vkWaitForFences(vulkanDevice, 1, &oldest.fence, VK_TRUE, UINT64_MAX);
    pollCompletedSubmissions();
  }

  // Step 3: Acquire a fence for this submission.
  VkFence fence = acquireFence();
  if (fence == VK_NULL_HANDLE) {
    LOGE("VulkanGPU::executeSubmission: fence allocation failed, reclaiming session.");
    reclaimAbandonedSession(std::move(request.session));
    for (auto& upload : request.uploads) {
      vmaDestroyBuffer(vmaAllocator, upload.stagingBuffer, upload.stagingAlloc);
    }
    return;
  }

  // Step 4: Build VkSubmitInfo. Two independent semaphore mechanisms may be active:
  //
  //   (a) Timeline semaphore — cross-Surface synchronization. When multiple Surfaces share a
  //       Context, Surface A's render result may be sampled as a texture by Surface B. The timeline
  //       semaphore ensures A's submission completes before B's fragment shader reads it.
  //       Wait stage: FRAGMENT_SHADER (vertex work can proceed in parallel).
  //
  //   (b) Binary semaphore pair — standard Vulkan presentation sync (acquire → render → present).
  //       imageAvailable: signaled by vkAcquireNextImageKHR, waited at COLOR_ATTACHMENT_OUTPUT.
  //       renderFinished: signaled by this submit, waited by vkQueuePresentKHR.
  //
  std::vector<VkSemaphore> waitSemaphores;
  std::vector<VkPipelineStageFlags> waitStages;
  std::vector<uint64_t> waitValues;
  std::vector<VkSemaphore> signalSemaphores;
  std::vector<uint64_t> signalValues;

  // (a) Timeline semaphore wait/signal for cross-Surface dependency.
  if (request.waitSemaphore) {
    waitSemaphores.push_back(request.waitSemaphore->vulkanSemaphore());
    waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    waitValues.push_back(request.waitSemaphore->signalValue());
  }
  if (request.signalSemaphore) {
    signalSemaphores.push_back(request.signalSemaphore->vulkanSemaphore());
    signalValues.push_back(request.signalSemaphore->nextSignalValue());
  }

  // (b) Binary semaphore pair for presentation sync.
  if (request.imageAvailableSemaphore != VK_NULL_HANDLE) {
    waitSemaphores.push_back(request.imageAvailableSemaphore);
    waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    waitValues.push_back(0);
  }
  if (request.renderFinishedSemaphore != VK_NULL_HANDLE) {
    signalSemaphores.push_back(request.renderFinishedSemaphore);
    signalValues.push_back(0);
  }

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = static_cast<uint32_t>(request.commandBuffers.size());
  submitInfo.pCommandBuffers = request.commandBuffers.data();
  if (!waitSemaphores.empty()) {
    submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
    submitInfo.pWaitSemaphores = waitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
  }
  if (!signalSemaphores.empty()) {
    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
    submitInfo.pSignalSemaphores = signalSemaphores.data();
  }

  VkTimelineSemaphoreSubmitInfo timelineInfo = {};
  timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
  if (request.waitSemaphore || request.signalSemaphore) {
    timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(waitValues.size());
    timelineInfo.pWaitSemaphoreValues = waitValues.data();
    timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
    timelineInfo.pSignalSemaphoreValues = signalValues.data();
    submitInfo.pNext = &timelineInfo;
  }

  // Step 5: Submit to the GPU queue.
  auto submitResult = vkQueueSubmit(vulkanQueue, 1, &submitInfo, fence);

  // Step 6a: On failure — fence was never signaled, so immediately reclaim all resources
  // through the same path as normal completion to avoid leaks.
  if (submitResult != VK_SUCCESS) {
    LOGE("VulkanGPU::submit: vkQueueSubmit failed (result=%d).", static_cast<int>(submitResult));
    if (submitResult == VK_ERROR_DEVICE_LOST) {
      contextLost = true;
    }
    InflightSubmission failed = {};
    failed.fence = fence;
    failed.session = std::move(request.session);
    failed.uploads = std::move(request.uploads);
    reclaimSubmission(failed);
    return;
  }

  // Step 6b: On success — record the submission as in-flight. Resources will be reclaimed
  // when pollCompletedSubmissions() or waitAllInflightSubmissions() observes the fence signal.
  InflightSubmission submission = {};
  submission.fence = fence;
  submission.frameTime = request.frameTime;
  submission.session = std::move(request.session);
  submission.uploads = std::move(request.uploads);
  // Retain semaphores in the submission so they outlive GPU execution.
  if (request.waitSemaphore) {
    submission.session.retainedResources.push_back(std::move(request.waitSemaphore));
  }
  if (request.signalSemaphore) {
    // Advance the timeline value only after a successful submit. If submit had failed, the value
    // stays unchanged so future waits on the uncommitted value don't deadlock.
    request.signalSemaphore->commitSignalValue();
    submission.session.retainedResources.push_back(std::move(request.signalSemaphore));
  }
  inflightSubmissions.push_back(std::move(submission));

  // Step 7: Execute pending present. The renderFinished semaphore ensures the presentation
  // engine waits for rendering to complete before displaying the image.
  if (request.presentSwapchain != VK_NULL_HANDLE) {
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &request.presentSwapchain;
    presentInfo.pImageIndices = &request.presentImageIndex;
    if (request.renderFinishedSemaphore != VK_NULL_HANDLE) {
      presentInfo.waitSemaphoreCount = 1;
      presentInfo.pWaitSemaphores = &request.renderFinishedSemaphore;
    }
    vkQueuePresentKHR(vulkanQueue, &presentInfo);
  }
}

void VulkanGPU::releaseAll(bool releaseGPU) {
  // Shutdown sequence must follow strict ordering to prevent use-after-free:
  //   waitAll -> queue -> fences -> caches -> resources -> VMA -> Device -> Instance
  // Each step depends on the subsequent resources still being valid.

  // 1. Wait for all in-flight submissions and release their retained resources. This must happen
  //    first so that retained shared_ptrs are decremented before we destroy the ReturnQueue.
  waitAllInflightSubmissions();

  // 2. Destroy the command queue (cleans up any pending uploads that were never submitted).
  commandQueue.reset();

  // 3. Destroy fence pool. Fences are no longer needed after all submissions are reclaimed.
  for (auto fence : fencePool) {
    vkDestroyFence(vulkanDevice, fence, nullptr);
  }
  fencePool.clear();

  // 4. Clear caches. Sampler cache holds shared_ptrs that may trigger ReturnQueue on release.
  samplerCache.clear();
  for (auto pool : descriptorPoolCache) {
    vkDestroyDescriptorPool(vulkanDevice, pool, nullptr);
  }
  descriptorPoolCache.clear();

  // 5. Release all remaining resources via onRelease(). After this, no VulkanResource exists.
  if (releaseGPU) {
    for (auto& resource : resources) {
      resource->onRelease(this);
    }
  }
  resources.clear();
  returnQueue = nullptr;

  // 6. Destroy device-level objects in reverse creation order.
  if (vmaAllocator != VK_NULL_HANDLE) {
    vmaDestroyAllocator(vmaAllocator);
    vmaAllocator = VK_NULL_HANDLE;
  }
  // When adopted, the caller owns the device and instance — do not destroy them.
  if (!adopted) {
    if (vulkanDevice != VK_NULL_HANDLE) {
      vkDestroyDevice(vulkanDevice, nullptr);
      vulkanDevice = VK_NULL_HANDLE;
    }
    if (vulkanInstance != VK_NULL_HANDLE) {
      vkDestroyInstance(vulkanInstance, nullptr);
      vulkanInstance = VK_NULL_HANDLE;
    }
  }
}

}  // namespace tgfx
