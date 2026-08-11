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

#if defined(__ANDROID__) || defined(_WIN32)

#include "VulkanHardwareTexture.h"

#if defined(__ANDROID__)
#include <android/hardware_buffer.h>
#endif

#if defined(_WIN32) && !defined(__ANDROID__)
#include <dxgi1_2.h>
#endif

#include "VulkanGPU.h"
#include "VulkanUtil.h"
#include "core/utils/Log.h"

namespace tgfx {

static VkFormat HardwareBufferFormatToVkFormat(HardwareBufferFormat format) {
  switch (format) {
    case HardwareBufferFormat::RGBA_8888:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case HardwareBufferFormat::ALPHA_8:
      return VK_FORMAT_R8_UNORM;
    case HardwareBufferFormat::BGRA_8888:
      return VK_FORMAT_B8G8R8A8_UNORM;
    case HardwareBufferFormat::YCBCR_420_SP:
      return VK_FORMAT_UNDEFINED;
    default:
      return VK_FORMAT_UNDEFINED;
  }
}

static uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t memoryTypeBits,
                               VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties = {};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((memoryTypeBits & (1u << i)) &&
        (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if (memoryTypeBits & (1u << i)) {
      return i;
    }
  }
  return UINT32_MAX;
}

#if defined(__ANDROID__)

std::shared_ptr<VulkanHardwareTexture> VulkanHardwareTexture::MakeFrom(
    VulkanGPU* gpu, HardwareBufferRef hardwareBuffer, uint32_t usage) {
  if (!gpu || !hardwareBuffer) {
    return nullptr;
  }
  if (!gpu->extensions().androidHardwareBuffer) {
    LOGE(
        "VulkanHardwareTexture::MakeFrom() VK_ANDROID_external_memory_android_hardware_buffer "
        "is not enabled on this device; importing an AHardwareBuffer requires this extension "
        "along with its dependencies (external memory, dedicated allocation, queue family "
        "foreign).");
    return nullptr;
  }
  if (!HardwareBufferCheck(hardwareBuffer)) {
    LOGE(
        "VulkanHardwareTexture::MakeFrom() HardwareBufferCheck rejected the buffer; the "
        "AHardwareBuffer system library is unavailable or the handle is invalid.");
    return nullptr;
  }

  auto info = HardwareBufferGetInfo(hardwareBuffer);
  if (info.width <= 0 || info.height <= 0) {
    return nullptr;
  }

  bool isYUV = (info.format == HardwareBufferFormat::YCBCR_420_SP);
  if (isYUV && !gpu->extensions().samplerYcbcrConversion) {
    LOGE("VulkanHardwareTexture::MakeFrom() YUV buffer requires YCbCr conversion support.");
    return nullptr;
  }
  if (isYUV && (usage & TextureUsage::RENDER_ATTACHMENT)) {
    LOGE(
        "VulkanHardwareTexture::MakeFrom() YUV hardware buffer cannot be used as render "
        "attachment.");
    return nullptr;
  }

  VkDevice device = gpu->device();
  VkPhysicalDevice physDevice = gpu->physicalDevice();

  VkAndroidHardwareBufferFormatPropertiesANDROID formatProps = {};
  formatProps.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;

  VkAndroidHardwareBufferPropertiesANDROID hwbProps = {};
  hwbProps.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
  hwbProps.pNext = &formatProps;

  auto result = vkGetAndroidHardwareBufferPropertiesANDROID(device, hardwareBuffer, &hwbProps);
  if (result != VK_SUCCESS) {
    LOGE("VulkanHardwareTexture::MakeFrom() vkGetAndroidHardwareBufferPropertiesANDROID failed: %s",
         VkResultToString(result));
    return nullptr;
  }

  VkFormat vkFormat = formatProps.format;
  bool useExternalFormat = (vkFormat == VK_FORMAT_UNDEFINED);
  if (!useExternalFormat && !isYUV) {
    VkFormat expectedFormat = HardwareBufferFormatToVkFormat(info.format);
    if (expectedFormat != VK_FORMAT_UNDEFINED) {
      vkFormat = expectedFormat;
    }
  }

  VkExternalMemoryImageCreateInfo externalMemoryInfo = {};
  externalMemoryInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
  externalMemoryInfo.handleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

  VkExternalFormatANDROID externalFormat = {};
  externalFormat.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
  if (useExternalFormat) {
    externalFormat.externalFormat = formatProps.externalFormat;
    externalFormat.pNext = nullptr;
    externalMemoryInfo.pNext = &externalFormat;
  }

  VkImageUsageFlags vkUsage = VK_IMAGE_USAGE_SAMPLED_BIT;
  if (!useExternalFormat) {
    vkUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (usage & TextureUsage::RENDER_ATTACHMENT) {
      vkUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
  }

  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.pNext = &externalMemoryInfo;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = useExternalFormat ? VK_FORMAT_UNDEFINED : vkFormat;
  imageInfo.extent = {static_cast<uint32_t>(info.width), static_cast<uint32_t>(info.height), 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = vkUsage;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage vkImage = VK_NULL_HANDLE;
  result = vkCreateImage(device, &imageInfo, nullptr, &vkImage);
  if (result != VK_SUCCESS) {
    LOGE("VulkanHardwareTexture::MakeFrom() vkCreateImage failed: %s", VkResultToString(result));
    return nullptr;
  }

  VkImportAndroidHardwareBufferInfoANDROID importInfo = {};
  importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
  importInfo.buffer = hardwareBuffer;

  VkMemoryDedicatedAllocateInfo dedicatedInfo = {};
  dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
  dedicatedInfo.pNext = &importInfo;
  dedicatedInfo.image = vkImage;

  uint32_t memoryTypeIndex =
      FindMemoryType(physDevice, hwbProps.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (memoryTypeIndex == UINT32_MAX) {
    LOGE("VulkanHardwareTexture::MakeFrom() no suitable memory type found.");
    vkDestroyImage(device, vkImage, nullptr);
    return nullptr;
  }

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.pNext = &dedicatedInfo;
  allocInfo.allocationSize = hwbProps.allocationSize;
  allocInfo.memoryTypeIndex = memoryTypeIndex;

  VkDeviceMemory memory = VK_NULL_HANDLE;
  result = vkAllocateMemory(device, &allocInfo, nullptr, &memory);
  if (result != VK_SUCCESS) {
    LOGE("VulkanHardwareTexture::MakeFrom() vkAllocateMemory failed: %s", VkResultToString(result));
    vkDestroyImage(device, vkImage, nullptr);
    return nullptr;
  }

  result = vkBindImageMemory(device, vkImage, memory, 0);
  if (result != VK_SUCCESS) {
    LOGE("VulkanHardwareTexture::MakeFrom() vkBindImageMemory failed: %s",
         VkResultToString(result));
    vkFreeMemory(device, memory, nullptr);
    vkDestroyImage(device, vkImage, nullptr);
    return nullptr;
  }

  VkSamplerYcbcrConversion ycbcrConversion = VK_NULL_HANDLE;
  if (useExternalFormat && !gpu->extensions().samplerYcbcrConversion) {
    LOGE("VulkanHardwareTexture::MakeFrom() external format requires YCbCr conversion support.");
    vkFreeMemory(device, memory, nullptr);
    vkDestroyImage(device, vkImage, nullptr);
    return nullptr;
  }
  if (useExternalFormat) {
    VkSamplerYcbcrConversionCreateInfo ycbcrInfo = {};
    ycbcrInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
    ycbcrInfo.format = VK_FORMAT_UNDEFINED;
    ycbcrInfo.ycbcrModel = formatProps.suggestedYcbcrModel;
    ycbcrInfo.ycbcrRange = formatProps.suggestedYcbcrRange;
    ycbcrInfo.components = formatProps.samplerYcbcrConversionComponents;
    ycbcrInfo.xChromaOffset = formatProps.suggestedXChromaOffset;
    ycbcrInfo.yChromaOffset = formatProps.suggestedYChromaOffset;
    ycbcrInfo.chromaFilter = (formatProps.formatFeatures &
                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT)
                                 ? VK_FILTER_LINEAR
                                 : VK_FILTER_NEAREST;
    ycbcrInfo.forceExplicitReconstruction = VK_FALSE;

    VkExternalFormatANDROID ycbcrExternalFormat = {};
    ycbcrExternalFormat.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
    ycbcrExternalFormat.externalFormat = formatProps.externalFormat;
    ycbcrInfo.pNext = &ycbcrExternalFormat;

    result = vkCreateSamplerYcbcrConversion(device, &ycbcrInfo, nullptr, &ycbcrConversion);
    if (result != VK_SUCCESS) {
      LOGE("VulkanHardwareTexture::MakeFrom() vkCreateSamplerYcbcrConversion failed: %s",
           VkResultToString(result));
      vkFreeMemory(device, memory, nullptr);
      vkDestroyImage(device, vkImage, nullptr);
      return nullptr;
    }
  }

  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = vkImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = useExternalFormat ? VK_FORMAT_UNDEFINED : vkFormat;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkSamplerYcbcrConversionInfo ycbcrViewInfo = {};
  if (ycbcrConversion != VK_NULL_HANDLE) {
    ycbcrViewInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
    ycbcrViewInfo.conversion = ycbcrConversion;
    viewInfo.pNext = &ycbcrViewInfo;
  }

  VkImageView vkImageView = VK_NULL_HANDLE;
  result = vkCreateImageView(device, &viewInfo, nullptr, &vkImageView);
  if (result != VK_SUCCESS) {
    LOGE("VulkanHardwareTexture::MakeFrom() vkCreateImageView failed: %s",
         VkResultToString(result));
    if (ycbcrConversion != VK_NULL_HANDLE) {
      vkDestroySamplerYcbcrConversion(device, ycbcrConversion, nullptr);
    }
    vkFreeMemory(device, memory, nullptr);
    vkDestroyImage(device, vkImage, nullptr);
    return nullptr;
  }

  PixelFormat pixelFormat =
      useExternalFormat ? PixelFormat::RGBA_8888 : VkFormatToPixelFormat(vkFormat);
  TextureDescriptor descriptor = {};
  descriptor.width = info.width;
  descriptor.height = info.height;
  descriptor.format = pixelFormat;
  descriptor.usage = usage;

  VkFormat reportedFormat = useExternalFormat ? VK_FORMAT_R8G8B8A8_UNORM : vkFormat;

  return gpu->makeResource<VulkanHardwareTexture>(descriptor, hardwareBuffer, vkImage, vkImageView,
                                                  memory, ycbcrConversion, reportedFormat,
                                                  VK_IMAGE_LAYOUT_UNDEFINED);
}

#elif defined(_WIN32)

// Opens the shared NT handle produced by IDXGIResource1::CreateSharedHandle as a VkDeviceMemory
// aliased over the same D3D11 texture memory. Returns VK_NULL_HANDLE on any failure; the caller
// is responsible for the VkImage lifecycle.
//
// The Vulkan spec (VUID-VkMemoryAllocateInfo-pNext-00639) requires that imported NT handles are
// duplicated by the driver: after vkAllocateMemory succeeds, the original handle must be closed
// by the caller. We do that here regardless of success to keep handle accounting linear.
static VkDeviceMemory ImportSharedNtHandleAsMemory(VkDevice device, VkPhysicalDevice physDevice,
                                                   HANDLE ntHandle, VkImage image,
                                                   const VkMemoryRequirements& imageMemReq) {
  VkMemoryWin32HandlePropertiesKHR handleProps = {};
  handleProps.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;
  auto hr = vkGetMemoryWin32HandlePropertiesKHR(
      device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, ntHandle, &handleProps);
  if (hr != VK_SUCCESS || handleProps.memoryTypeBits == 0) {
    LOGE("VulkanHardwareTexture: vkGetMemoryWin32HandlePropertiesKHR failed: %s",
         VkResultToString(hr));
    return VK_NULL_HANDLE;
  }
  // Intersect with the image's own compatible mask so the resulting index passes vkBindImageMemory
  // validation. If the intersection is empty, fall back to the image mask alone (some drivers
  // return an over-restrictive mask for imported handles).
  uint32_t typeBits = handleProps.memoryTypeBits & imageMemReq.memoryTypeBits;
  if (typeBits == 0) {
    typeBits = imageMemReq.memoryTypeBits;
  }
  uint32_t memoryTypeIndex =
      FindMemoryType(physDevice, typeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (memoryTypeIndex == UINT32_MAX) {
    LOGE("VulkanHardwareTexture: no suitable memory type for imported NT handle.");
    return VK_NULL_HANDLE;
  }

  VkImportMemoryWin32HandleInfoKHR importInfo = {};
  importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
  importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
  importInfo.handle = ntHandle;
  importInfo.name = nullptr;

  VkMemoryDedicatedAllocateInfo dedicatedInfo = {};
  dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
  dedicatedInfo.pNext = &importInfo;
  dedicatedInfo.image = image;

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.pNext = &dedicatedInfo;
  allocInfo.allocationSize = imageMemReq.size;
  allocInfo.memoryTypeIndex = memoryTypeIndex;

  VkDeviceMemory memory = VK_NULL_HANDLE;
  auto ar = vkAllocateMemory(device, &allocInfo, nullptr, &memory);
  if (ar != VK_SUCCESS) {
    LOGE("VulkanHardwareTexture: vkAllocateMemory (imported NT handle) failed: %s",
         VkResultToString(ar));
    return VK_NULL_HANDLE;
  }
  return memory;
}

// Performs a synchronous, one-shot queue-family-ownership acquire + layout transition on a
// freshly-imported image. tgfx's Vulkan render pass code samples every texture assuming
// VK_IMAGE_LAYOUT_GENERAL (see VulkanRenderPass.cpp), but an image bound to imported memory
// starts in VK_IMAGE_LAYOUT_UNDEFINED and would be discarded by a regular UNDEFINED->GENERAL
// barrier. The trick is srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL: the Vulkan spec treats
// this pattern as an ownership-transfer from an external API and preserves the underlying
// content instead of discarding it.
//
// Because this only runs once per imported texture, the extra vkQueueWaitIdle here is bounded
// and acceptable; the alternative of deferring the barrier to the first render pass would
// require intrusive changes to the render-pass lifecycle to insert pre-pass barriers.
//
// The keyed mutex on the imported memory MUST also be acquired and released around this submit,
// because the D3D11 producer still owns the memory otherwise. Keys are 0, matching the runtime
// contract used by the render path.
static bool AcquireExternalOwnershipAndTransitionToGeneral(VulkanGPU* gpu, VkImage image,
                                                           VkDeviceMemory memory) {
  VkDevice device = gpu->device();
  VkQueue queue = gpu->graphicsQueue();

  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = gpu->graphicsQueueIndex();
  if (vkCreateCommandPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
    return false;
  }

  VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo cmdAllocInfo = {};
  cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdAllocInfo.commandPool = pool;
  cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdAllocInfo.commandBufferCount = 1;
  if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmdBuf) != VK_SUCCESS) {
    vkDestroyCommandPool(device, pool, nullptr);
    return false;
  }

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(cmdBuf, &beginInfo) != VK_SUCCESS) {
    vkFreeCommandBuffers(device, pool, 1, &cmdBuf);
    vkDestroyCommandPool(device, pool, nullptr);
    return false;
  }

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  // Both layouts are GENERAL: this call is really a queue-family-ownership acquire, not a
  // content-changing layout transition. Using UNDEFINED as oldLayout is spec-compliant for a
  // fresh image but authorises the driver to discard content, which is exactly what we saw on
  // NVIDIA. Declaring GENERAL on both sides tells the driver "the external producer already
  // has the image in a shader-readable layout; just take ownership".
  barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  // Acquire ownership from the external (D3D11) API.
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL_KHR;
  barrier.dstQueueFamilyIndex = gpu->graphicsQueueIndex();
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);
  vkEndCommandBuffer(cmdBuf);

  // Chain a keyed-mutex acquire/release around the submit so the D3D11 side does not race with
  // thisownership-transfer. Key 0, timeout 5s — same numbers used everywhere else on this
  // code path.
  VkWin32KeyedMutexAcquireReleaseInfoKHR keyedMutexInfo = {};
  uint64_t key = 0;
  uint32_t timeout = 5000;
  keyedMutexInfo.sType = VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_KHR;
  keyedMutexInfo.acquireCount = 1;
  keyedMutexInfo.pAcquireSyncs = &memory;
  keyedMutexInfo.pAcquireKeys = &key;
  keyedMutexInfo.pAcquireTimeouts = &timeout;
  keyedMutexInfo.releaseCount = 1;
  keyedMutexInfo.pReleaseSyncs = &memory;
  keyedMutexInfo.pReleaseKeys = &key;

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.pNext = &keyedMutexInfo;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmdBuf;

  auto sr = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
  if (sr != VK_SUCCESS) {
    LOGE("VulkanHardwareTexture: immediate acquire submit failed: %s", VkResultToString(sr));
    vkFreeCommandBuffers(device, pool, 1, &cmdBuf);
    vkDestroyCommandPool(device, pool, nullptr);
    return false;
  }
  // Blocking wait: OK because this is a one-shot initialisation. The graphics queue is single-
  // threaded from tgfx's perspective (all callers hold the Context lock), so no cross-thread
  // interference is possible.
  vkQueueWaitIdle(queue);
  vkFreeCommandBuffers(device, pool, 1, &cmdBuf);
  vkDestroyCommandPool(device, pool, nullptr);
  return true;
}

std::shared_ptr<VulkanHardwareTexture> VulkanHardwareTexture::MakeFrom(
    VulkanGPU* gpu, HardwareBufferRef hardwareBuffer, uint32_t usage) {
  if (!gpu || !hardwareBuffer) {
    return nullptr;
  }
  if (!gpu->extensions().win32ExternalMemory || !gpu->extensions().win32KeyedMutex) {
    LOGE(
        "VulkanHardwareTexture::MakeFrom() Win32 external memory / keyed mutex extensions are "
        "not enabled on this device; importing a D3D11 texture requires "
        "VK_KHR_external_memory_win32 + VK_KHR_win32_keyed_mutex.");
    return nullptr;
  }
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return nullptr;
  }
  if (usage & TextureUsage::RENDER_ATTACHMENT) {
    LOGE(
        "VulkanHardwareTexture::MakeFrom() RENDER_ATTACHMENT is not supported for imported D3D11 "
        "textures on the Vulkan-on-Windows backend (contract mirrors the D3D12 v1 behaviour).");
    return nullptr;
  }

  auto info = HardwareBufferGetInfo(hardwareBuffer);
  VkFormat vkFormat = HardwareBufferFormatToVkFormat(info.format);
  if (vkFormat == VK_FORMAT_UNDEFINED || info.format == HardwareBufferFormat::YCBCR_420_SP) {
    // NV12 is out of scope for the v1 Win32 path (see the D3D12 backend contract).
    return nullptr;
  }

  auto* textureUnk = reinterpret_cast<IUnknown*>(hardwareBuffer);
  Microsoft::WRL::ComPtr<IDXGIResource1> dxgiResource1 = nullptr;
  if (FAILED(textureUnk->QueryInterface(IID_PPV_ARGS(&dxgiResource1))) || !dxgiResource1) {
    LOGE(
        "VulkanHardwareTexture::MakeFrom() the source ID3D11Texture2D does not implement "
        "IDXGIResource1; recreate it with MISC_SHARED_NTHANDLE | MISC_SHARED_KEYEDMUTEX.");
    return nullptr;
  }
  HANDLE ntHandle = nullptr;
  auto hr =
      dxgiResource1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &ntHandle);
  if (FAILED(hr) || ntHandle == nullptr) {
    LOGE(
        "VulkanHardwareTexture::MakeFrom() IDXGIResource1::CreateSharedHandle failed "
        "(HRESULT=0x%08X); the source ID3D11Texture2D is not shareable via NT handle.",
        static_cast<unsigned>(hr));
    return nullptr;
  }

  Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyedMutex = nullptr;
  hr = textureUnk->QueryInterface(IID_PPV_ARGS(&keyedMutex));
  if (FAILED(hr) || keyedMutex == nullptr) {
    CloseHandle(ntHandle);
    LOGE(
        "VulkanHardwareTexture::MakeFrom() shared texture does not expose IDXGIKeyedMutex; "
        "MISC_SHARED_KEYEDMUTEX is required alongside MISC_SHARED_NTHANDLE.");
    return nullptr;
  }

  VkDevice device = gpu->device();
  VkPhysicalDevice physDevice = gpu->physicalDevice();

  VkExternalMemoryImageCreateInfo externalMemoryInfo = {};
  externalMemoryInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
  externalMemoryInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;

  // Precheck: ask the driver whether a VkImage with this exact configuration can be backed by
  // memory imported from a D3D11 shared NT handle. If it cannot, we bail out immediately with a
  // detailed log — allocating anyway would produce an image whose sampled contents are
  // driver-defined (typically all zeros on NVIDIA, matching the symptom we chased in the initial
  // implementation).
  {
    VkPhysicalDeviceExternalImageFormatInfo extFormatInfo = {};
    extFormatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    extFormatInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;

    VkPhysicalDeviceImageFormatInfo2 formatInfo = {};
    formatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    formatInfo.pNext = &extFormatInfo;
    formatInfo.format = vkFormat;
    formatInfo.type = VK_IMAGE_TYPE_2D;
    formatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    formatInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    formatInfo.flags = 0;

    VkExternalImageFormatProperties extFormatProps = {};
    extFormatProps.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;

    VkImageFormatProperties2 formatProps = {};
    formatProps.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    formatProps.pNext = &extFormatProps;

    auto queryFn = vkGetPhysicalDeviceImageFormatProperties2
                       ? vkGetPhysicalDeviceImageFormatProperties2
                       : vkGetPhysicalDeviceImageFormatProperties2KHR;
    if (queryFn == nullptr) {
      CloseHandle(ntHandle);
      LOGE(
          "VulkanHardwareTexture::MakeFrom() vkGetPhysicalDeviceImageFormatProperties2 is "
          "unavailable; cannot verify external-image compatibility.");
      return nullptr;
    }
    auto qr = queryFn(gpu->physicalDevice(), &formatInfo, &formatProps);
    if (qr != VK_SUCCESS) {
      CloseHandle(ntHandle);
      LOGE(
          "VulkanHardwareTexture::MakeFrom() vkGetPhysicalDeviceImageFormatProperties2 rejected "
          "the D3D11-shared BGRA/RGBA import (result=%s). This driver does not support importing "
          "the given format/tiling/usage combination from a D3D11 NT handle.",
          VkResultToString(qr));
      return nullptr;
    }
    auto compatBits = extFormatProps.externalMemoryProperties.compatibleHandleTypes;
    auto features = extFormatProps.externalMemoryProperties.externalMemoryFeatures;
    if ((compatBits & VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT) == 0 ||
        (features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) == 0) {
      CloseHandle(ntHandle);
      LOGE(
          "VulkanHardwareTexture::MakeFrom() driver reports D3D11_TEXTURE handle type is not "
          "importable for this image configuration (compatBits=0x%08X features=0x%08X). Sampled "
          "content would be undefined; refusing the import.",
          static_cast<unsigned>(compatBits), static_cast<unsigned>(features));
      return nullptr;
    }
    // Some drivers (notably NVIDIA) additionally require dedicated allocation for imported
    // images; VkMemoryDedicatedAllocateInfo below already covers that requirement.
    (void)features;
  }

  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.pNext = &externalMemoryInfo;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = vkFormat;
  imageInfo.extent = {static_cast<uint32_t>(info.width), static_cast<uint32_t>(info.height), 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage vkImage = VK_NULL_HANDLE;
  auto vkr = vkCreateImage(device, &imageInfo, nullptr, &vkImage);
  if (vkr != VK_SUCCESS) {
    CloseHandle(ntHandle);
    LOGE("VulkanHardwareTexture::MakeFrom() vkCreateImage failed: %s", VkResultToString(vkr));
    return nullptr;
  }

  VkMemoryRequirements imageMemReq = {};
  vkGetImageMemoryRequirements(device, vkImage, &imageMemReq);

  VkDeviceMemory memory =
      ImportSharedNtHandleAsMemory(device, physDevice, ntHandle, vkImage, imageMemReq);
  // Regardless of success, CloseHandle balances the CreateSharedHandle duplicate. On success the
  // driver has taken its own reference; on failure we simply drop ours here.
  CloseHandle(ntHandle);
  if (memory == VK_NULL_HANDLE) {
    vkDestroyImage(device, vkImage, nullptr);
    return nullptr;
  }

  vkr = vkBindImageMemory(device, vkImage, memory, 0);
  if (vkr != VK_SUCCESS) {
    LOGE("VulkanHardwareTexture::MakeFrom() vkBindImageMemory failed: %s", VkResultToString(vkr));
    vkFreeMemory(device, memory, nullptr);
    vkDestroyImage(device, vkImage, nullptr);
    return nullptr;
  }

  // Immediately transition the image to GENERAL layout via an external-queue-family ownership
  // acquire, so that subsequent samples (which tgfx unconditionally binds at GENERAL) find the
  // image content preserved. Without this the very first sample of a freshly-imported texture
  // reads zeros, because the driver treats UNDEFINED->GENERAL from the graphics queue as a
  // discard.
  if (!AcquireExternalOwnershipAndTransitionToGeneral(gpu, vkImage, memory)) {
    LOGE("VulkanHardwareTexture::MakeFrom() failed to transition imported image to GENERAL.");
    vkFreeMemory(device, memory, nullptr);
    vkDestroyImage(device, vkImage, nullptr);
    return nullptr;
  }

  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = vkImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = vkFormat;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkImageView vkImageView = VK_NULL_HANDLE;
  vkr = vkCreateImageView(device, &viewInfo, nullptr, &vkImageView);
  if (vkr != VK_SUCCESS) {
    LOGE("VulkanHardwareTexture::MakeFrom() vkCreateImageView failed: %s", VkResultToString(vkr));
    vkFreeMemory(device, memory, nullptr);
    vkDestroyImage(device, vkImage, nullptr);
    return nullptr;
  }

  PixelFormat pixelFormat = VkFormatToPixelFormat(vkFormat);
  TextureDescriptor descriptor = {};
  descriptor.width = info.width;
  descriptor.height = info.height;
  descriptor.format = pixelFormat;
  descriptor.usage = usage;

  auto texture = gpu->makeResource<VulkanHardwareTexture>(descriptor, hardwareBuffer, vkImage,
                                                          vkImageView, memory, VK_NULL_HANDLE,
                                                          vkFormat, VK_IMAGE_LAYOUT_GENERAL);
  if (texture) {
    texture->_keyedMutex = keyedMutex;
  }
  return texture;
}

#endif  // platform switch for MakeFrom

VulkanHardwareTexture::VulkanHardwareTexture(const TextureDescriptor& descriptor,
                                             HardwareBufferRef hardwareBuffer, VkImage image,
                                             VkImageView imageView, VkDeviceMemory dedicatedMemory,
                                             VkSamplerYcbcrConversion ycbcrConversion,
                                             VkFormat format, VkImageLayout initialLayout)
    : VulkanTexture(descriptor, image, imageView, VK_NULL_HANDLE, VK_NULL_HANDLE, format, true,
                    initialLayout),
      hardwareBuffer(hardwareBuffer), dedicatedMemory(dedicatedMemory),
      ycbcrConversion(ycbcrConversion) {
  HardwareBufferRetain(hardwareBuffer);
}

VulkanHardwareTexture::~VulkanHardwareTexture() {
  HardwareBufferRelease(hardwareBuffer);
}

void VulkanHardwareTexture::onRelease(VulkanGPU* gpu) {
  VkDevice device = gpu->device();
  if (ycbcrConversion != VK_NULL_HANDLE) {
    vkDestroySamplerYcbcrConversion(device, ycbcrConversion, nullptr);
    ycbcrConversion = VK_NULL_HANDLE;
  }
  VulkanTexture::onRelease(gpu);
  if (dedicatedMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, dedicatedMemory, nullptr);
    dedicatedMemory = VK_NULL_HANDLE;
  }
}

}  // namespace tgfx

#endif  // defined(__ANDROID__) || defined(_WIN32)
