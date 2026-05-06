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

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "vk_mem_alloc.h"

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
  if (hasTimelineSemaphore) {
    deviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
  }
  // Required for dynamic primitive topology (VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT), which
  // allows draw calls to switch between TriangleList and TriangleStrip without separate pipelines.
  if (hasExtendedDynamicState) {
    deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
  }

  VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures = {};
  timelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  timelineFeatures.timelineSemaphore = hasTimelineSemaphore ? VK_TRUE : VK_FALSE;

  VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynStateFeatures = {};
  extDynStateFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
  extDynStateFeatures.extendedDynamicState = hasExtendedDynamicState ? VK_TRUE : VK_FALSE;

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
  if (!backendTexture.getVulkanTextureInfo(&vulkanInfo) || vulkanInfo.image == nullptr) {
    return nullptr;
  }
  return VulkanTexture::MakeFrom(this, static_cast<VkImage>(vulkanInfo.image),
                                 static_cast<VkFormat>(vulkanInfo.format), backendTexture.width(),
                                 backendTexture.height(), usage, adopted);
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
  return VulkanTexture::MakeFrom(this, static_cast<VkImage>(vulkanInfo.image),
                                 static_cast<VkFormat>(vulkanInfo.format),
                                 backendRenderTarget.width(), backendRenderTarget.height(),
                                 TextureUsage::RENDER_ATTACHMENT, false);
}

std::shared_ptr<Semaphore> VulkanGPU::importBackendSemaphore(const BackendSemaphore& semaphore) {
  if (semaphore.backend() != Backend::Vulkan) {
    return nullptr;
  }
  VulkanSyncInfo vulkanInfo = {};
  if (!semaphore.getVulkanSync(&vulkanInfo) || vulkanInfo.semaphore == nullptr) {
    return nullptr;
  }
  return VulkanSemaphore::MakeFrom(this, static_cast<VkSemaphore>(vulkanInfo.semaphore),
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
  key |= static_cast<uint32_t>(descriptor.addressModeX);
  key |= static_cast<uint32_t>(descriptor.addressModeY) << 3;
  key |= static_cast<uint32_t>(descriptor.minFilter) << 6;
  key |= static_cast<uint32_t>(descriptor.magFilter) << 8;
  key |= static_cast<uint32_t>(descriptor.mipmapMode) << 10;
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

// Pool capacity constants for per-frame descriptor set allocation.
// Reference values from industry 2D/3D engines:
//   Skia Graphite: dynamic pool chain (no fixed cap)
//   Filament: 4096 sets per frame
//   bgfx: 2048 sets per frame
//   Godot 4: 4096 sets per frame
//   wgpu/Dawn: 8192 sets per frame
static constexpr uint32_t POOL_MAX_DESCRIPTOR_SETS = 8192;
static constexpr uint32_t POOL_MAX_UNIFORM_BUFFERS = 16384;
static constexpr uint32_t POOL_MAX_COMBINED_SAMPLERS = 8192;

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

void VulkanGPU::releaseAll(bool releaseGPU) {
  samplerCache.clear();
  for (auto pool : descriptorPoolCache) {
    vkDestroyDescriptorPool(vulkanDevice, pool, nullptr);
  }
  descriptorPoolCache.clear();
  if (releaseGPU) {
    for (auto& resource : resources) {
      resource->onRelease(this);
    }
  }
  resources.clear();
  returnQueue = nullptr;

  if (vmaAllocator != VK_NULL_HANDLE) {
    vmaDestroyAllocator(vmaAllocator);
    vmaAllocator = VK_NULL_HANDLE;
  }
  if (vulkanDevice != VK_NULL_HANDLE) {
    vkDestroyDevice(vulkanDevice, nullptr);
    vulkanDevice = VK_NULL_HANDLE;
  }
  if (vulkanInstance != VK_NULL_HANDLE) {
    vkDestroyInstance(vulkanInstance, nullptr);
    vulkanInstance = VK_NULL_HANDLE;
  }
}

}  // namespace tgfx
