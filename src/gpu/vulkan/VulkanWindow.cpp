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

#include "tgfx/gpu/vulkan/VulkanWindow.h"
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __OHOS__
#include <native_window/external_window.h>
#endif
#ifdef __ANDROID__
#include <android/native_window.h>
#endif
#include <vector>
#include "core/utils/Log.h"
#include "gpu/vulkan/VulkanAPI.h"
#include "gpu/vulkan/VulkanDrawable.h"
#include "gpu/vulkan/VulkanGPU.h"
#include "gpu/vulkan/VulkanSwapchainProxy.h"
#include "gpu/vulkan/VulkanUtil.h"

namespace tgfx {

static void DestroySwapchainResources(VkDevice device, VkInstance instance, VkSurfaceKHR surface,
                                      VkSwapchainKHR swapchain) {
  if (swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device, swapchain, nullptr);
  }
  if (surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance, surface, nullptr);
  }
}

static std::vector<std::shared_ptr<VulkanSwapchainImageState>> CreateImageStates(VkDevice device,
                                                                                 size_t count) {
  std::vector<std::shared_ptr<VulkanSwapchainImageState>> states;
  states.reserve(count);
  VkSemaphoreCreateInfo semaphoreInfo = {};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  for (size_t i = 0; i < count; i++) {
    auto state = std::make_shared<VulkanSwapchainImageState>();
    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &state->presentSemaphore) !=
        VK_SUCCESS) {
      for (auto& createdState : states) {
        vkDestroySemaphore(device, createdState->presentSemaphore, nullptr);
      }
      return {};
    }
    states.push_back(std::move(state));
  }
  return states;
}

static void DestroyImageStates(VkDevice device,
                               std::vector<std::shared_ptr<VulkanSwapchainImageState>>& states) {
  for (auto& state : states) {
    if (state->presentSemaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(device, state->presentSemaphore, nullptr);
      state->presentSemaphore = VK_NULL_HANDLE;
    }
  }
  states.clear();
}

// Picks the swapchain present mode for the requested vsync setting. FIFO is guaranteed by the
// spec and used whenever vsync is enabled. When vsync is disabled, MAILBOX is preferred (tear-free,
// low latency) and IMMEDIATE is the fallback; if neither is advertised the code stays on FIFO so
// creation always succeeds. Guarded by the same platform macros as its callers (the MakeFrom
// overloads) so it is not compiled as an unused function on platforms without a MakeFrom.
#if defined(_WIN32) || defined(__OHOS__) || defined(__ANDROID__)
static VkPresentModeKHR ChoosePresentMode(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                          bool vsyncEnabled) {
  if (vsyncEnabled) {
    return VK_PRESENT_MODE_FIFO_KHR;
  }
  uint32_t count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, nullptr);
  if (count == 0) {
    return VK_PRESENT_MODE_FIFO_KHR;
  }
  std::vector<VkPresentModeKHR> modes(count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, modes.data());
  bool hasMailbox = false;
  bool hasImmediate = false;
  for (auto mode : modes) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      hasMailbox = true;
    } else if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
      hasImmediate = true;
    }
  }
  if (hasMailbox) {
    return VK_PRESENT_MODE_MAILBOX_KHR;
  }
  if (hasImmediate) {
    return VK_PRESENT_MODE_IMMEDIATE_KHR;
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}
#endif

// Holds all Vulkan-specific handles and swapchain resources. Defined here (not in the header) so
// that vulkan.h is never required by downstream translation units that include VulkanWindow.h.
struct VulkanWindow::PlatformState {
  VkDevice cachedDevice = VK_NULL_HANDLE;
  VkInstance cachedInstance = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  std::vector<VkImage> images;
  std::vector<std::shared_ptr<VulkanSwapchainImageState>> imageStates;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
  int width = 0;
  int height = 0;
  bool supportsReadback = false;
  std::shared_ptr<bool> outOfDate = std::make_shared<bool>(false);
  std::shared_ptr<VulkanManualToken> manualToken = std::make_shared<VulkanManualToken>();
  std::shared_ptr<uint64_t> generation = std::make_shared<uint64_t>(0);
  std::vector<std::weak_ptr<VulkanSwapchainProxy>> activeProxies;

  bool recreateSwapchain(VkDevice device, const VkSurfaceCapabilitiesKHR& capabilities,
                         const VkExtent2D& extent);
};

#ifdef _WIN32

std::shared_ptr<VulkanWindow> VulkanWindow::MakeFrom(HWND hwnd,
                                                     std::shared_ptr<VulkanDevice> device,
                                                     std::shared_ptr<ColorSpace> colorSpace,
                                                     bool vsyncEnabled) {
  if (hwnd == nullptr || device == nullptr) {
    return nullptr;
  }

  if (colorSpace && !colorSpace->isSRGB()) {
    LOGI(
        "VulkanWindow::MakeFrom(): non-sRGB colorSpace is not yet supported and will be ignored. "
        "Only sRGB output is currently available.");
  }

  auto vulkanGPU = static_cast<VulkanGPU*>(device->lockContext()->gpu());

  if (!vulkanGPU->extensions().swapchain) {
    LOGE("VulkanWindow: swapchain extension not available; cannot create window surface.");
    device->unlock();
    return nullptr;
  }

  auto vkInstance = vulkanGPU->instance();
  auto vkDevice = vulkanGPU->device();
  auto physicalDevice = vulkanGPU->physicalDevice();

  VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
  surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surfaceInfo.hinstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
  surfaceInfo.hwnd = hwnd;

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  auto result = vkCreateWin32SurfaceKHR(vkInstance, &surfaceInfo, nullptr, &surface);
  if (result != VK_SUCCESS) {
    LOGE("VulkanWindow: vkCreateWin32SurfaceKHR failed: %s", VkResultToString(result));
    device->unlock();
    return nullptr;
  }

  VkBool32 presentSupport = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, vulkanGPU->graphicsQueueIndex(), surface,
                                       &presentSupport);
  if (!presentSupport) {
    LOGE("VulkanWindow: graphics queue does not support presentation.");
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }

  VkSurfaceCapabilitiesKHR capabilities = {};
  result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
  if (result != VK_SUCCESS) {
    LOGE("VulkanWindow: vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed: %s",
         VkResultToString(result));
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }

  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
  if (formatCount == 0) {
    LOGE("VulkanWindow: no surface formats available.");
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

  // TODO: The colorSpace parameter is currently unused. Swapchain format is hardcoded to
  // B8G8R8A8_UNORM + SRGB_NONLINEAR, consistent with Metal and OpenGL backends which also default
  // to sRGB. HDR / Display-P3 support would require a ColorSpace → VkColorSpaceKHR mapping here.
  // Spec allows a single {VK_FORMAT_UNDEFINED, SRGB_NONLINEAR} entry meaning "any format".
  VkSurfaceFormatKHR chosenFormat = formats[0];
  if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
    chosenFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  } else {
    for (auto& f : formats) {
      if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
          f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        chosenFormat = f;
        break;
      }
    }
  }

  VkExtent2D extent = capabilities.currentExtent;
  if (extent.width == 0xFFFFFFFF) {
    RECT rect = {};
    GetClientRect(hwnd, &rect);
    extent.width = static_cast<uint32_t>(rect.right - rect.left);
    extent.height = static_cast<uint32_t>(rect.bottom - rect.top);
  }

  uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
  }

  // Intersect desired usage with what the surface actually supports. TRANSFER_DST is optional
  // (some Android drivers don't advertise it), but COLOR_ATTACHMENT is mandatory for rendering.
  VkImageUsageFlags desiredUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  VkImageUsageFlags imageUsage = desiredUsage & capabilities.supportedUsageFlags;
  if (!(imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
    LOGE("VulkanWindow: surface does not support COLOR_ATTACHMENT usage.");
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }

  VkSwapchainCreateInfoKHR swapchainInfo = {};
  swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainInfo.surface = surface;
  swapchainInfo.minImageCount = imageCount;
  swapchainInfo.imageFormat = chosenFormat.format;
  swapchainInfo.imageColorSpace = chosenFormat.colorSpace;
  swapchainInfo.imageExtent = extent;
  swapchainInfo.imageArrayLayers = 1;
  swapchainInfo.imageUsage = imageUsage;
  swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchainInfo.preTransform = capabilities.currentTransform;
  swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  auto presentMode = ChoosePresentMode(physicalDevice, surface, vsyncEnabled);
  swapchainInfo.presentMode = presentMode;
  swapchainInfo.clipped = VK_TRUE;

  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  result = vkCreateSwapchainKHR(vkDevice, &swapchainInfo, nullptr, &swapchain);
  if (result != VK_SUCCESS) {
    LOGE("VulkanWindow: vkCreateSwapchainKHR failed: %s", VkResultToString(result));
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }

  uint32_t swapImageCount = 0;
  vkGetSwapchainImagesKHR(vkDevice, swapchain, &swapImageCount, nullptr);
  std::vector<VkImage> images(swapImageCount);
  vkGetSwapchainImagesKHR(vkDevice, swapchain, &swapImageCount, images.data());

  device->unlock();

  auto imageStates = CreateImageStates(vkDevice, images.size());
  if (images.empty() || imageStates.size() != images.size()) {
    DestroySwapchainResources(vkDevice, vkInstance, surface, swapchain);
    return nullptr;
  }
  auto state = std::make_unique<PlatformState>();
  state->cachedDevice = vkDevice;
  state->cachedInstance = vkInstance;
  state->surface = surface;
  state->swapchain = swapchain;
  state->images = std::move(images);
  state->imageStates = std::move(imageStates);
  state->format = chosenFormat.format;
  state->colorSpace = chosenFormat.colorSpace;
  state->compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  state->presentMode = presentMode;
  state->width = static_cast<int>(extent.width);
  state->height = static_cast<int>(extent.height);
  state->supportsReadback = VulkanSupportsReadback(imageUsage);

  return std::shared_ptr<VulkanWindow>(
      new VulkanWindow(device, std::move(state), colorSpace, vsyncEnabled));
}

#endif

#ifdef __OHOS__

std::shared_ptr<VulkanWindow> VulkanWindow::MakeFrom(OHNativeWindow* nativeWindow,
                                                     std::shared_ptr<VulkanDevice> device,
                                                     std::shared_ptr<ColorSpace> colorSpace,
                                                     bool vsyncEnabled) {
  if (nativeWindow == nullptr || device == nullptr) {
    return nullptr;
  }

  if (colorSpace && !colorSpace->isSRGB()) {
    LOGI(
        "VulkanWindow::MakeFrom(): non-sRGB colorSpace is not yet supported and will be ignored. "
        "Only sRGB output is currently available.");
  }

  auto vulkanGPU = static_cast<VulkanGPU*>(device->lockContext()->gpu());

  if (!vulkanGPU->extensions().swapchain) {
    LOGE("VulkanWindow: swapchain extension not available; cannot create window surface.");
    device->unlock();
    return nullptr;
  }

  auto vkInstance = vulkanGPU->instance();
  auto vkDevice = vulkanGPU->device();
  auto physicalDevice = vulkanGPU->physicalDevice();

  VkSurfaceCreateInfoOHOS surfaceInfo = {};
  surfaceInfo.sType = VK_STRUCTURE_TYPE_SURFACE_CREATE_INFO_OHOS;
  surfaceInfo.window = nativeWindow;

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  // volk does not include OHOS surface support, so we load vkCreateSurfaceOHOS manually.
  auto createSurfaceOHOS = reinterpret_cast<PFN_vkCreateSurfaceOHOS>(
      vkGetInstanceProcAddr(vkInstance, "vkCreateSurfaceOHOS"));
  if (!createSurfaceOHOS) {
    LOGE("VulkanWindow: vkCreateSurfaceOHOS not available.");
    device->unlock();
    return nullptr;
  }
  auto result = createSurfaceOHOS(vkInstance, &surfaceInfo, nullptr, &surface);
  if (result != VK_SUCCESS) {
    LOGE("VulkanWindow: vkCreateSurfaceOHOS failed: %s", VkResultToString(result));
    device->unlock();
    return nullptr;
  }

  VkBool32 presentSupport = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, vulkanGPU->graphicsQueueIndex(), surface,
                                       &presentSupport);
  if (!presentSupport) {
    LOGE("VulkanWindow: graphics queue does not support presentation.");
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }

  VkSurfaceCapabilitiesKHR capabilities = {};
  result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
  if (result != VK_SUCCESS) {
    LOGE("VulkanWindow: vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed: %s",
         VkResultToString(result));
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }

  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
  if (formatCount == 0) {
    LOGE("VulkanWindow: no surface formats available.");
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

  // TODO: The colorSpace parameter is currently unused. Swapchain format is hardcoded to
  // B8G8R8A8_UNORM + SRGB_NONLINEAR, consistent with Metal and OpenGL backends which also default
  // to sRGB. HDR / Display-P3 support would require a ColorSpace → VkColorSpaceKHR mapping here.
  // Spec allows a single {VK_FORMAT_UNDEFINED, SRGB_NONLINEAR} entry meaning "any format".
  VkSurfaceFormatKHR chosenFormat = formats[0];
  if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
    chosenFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  } else {
    for (auto& f : formats) {
      if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
          f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        chosenFormat = f;
        break;
      }
    }
  }

  VkExtent2D extent = capabilities.currentExtent;
  if (extent.width == 0xFFFFFFFF) {
    int32_t width = 0;
    int32_t height = 0;
    OH_NativeWindow_NativeWindowHandleOpt(nativeWindow, GET_BUFFER_GEOMETRY, &height, &width);
    extent.width = static_cast<uint32_t>(width);
    extent.height = static_cast<uint32_t>(height);
  }

  uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
  }

  VkImageUsageFlags desiredUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  VkImageUsageFlags imageUsage = desiredUsage & capabilities.supportedUsageFlags;
  if (!(imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
    LOGE("VulkanWindow: surface does not support COLOR_ATTACHMENT usage.");
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }

  VkSwapchainCreateInfoKHR swapchainInfo = {};
  swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainInfo.surface = surface;
  swapchainInfo.minImageCount = imageCount;
  swapchainInfo.imageFormat = chosenFormat.format;
  swapchainInfo.imageColorSpace = chosenFormat.colorSpace;
  swapchainInfo.imageExtent = extent;
  swapchainInfo.imageArrayLayers = 1;
  swapchainInfo.imageUsage = imageUsage;
  swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchainInfo.preTransform = capabilities.currentTransform;
  // Prefer INHERIT so the OHOS compositor controls alpha blending (e.g. system navigation bar
  // transparency). Fall back to OPAQUE if the device does not advertise INHERIT support.
  VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  if (!(capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)) {
    compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  }
  swapchainInfo.compositeAlpha = compositeAlpha;
  auto presentMode = ChoosePresentMode(physicalDevice, surface, vsyncEnabled);
  swapchainInfo.presentMode = presentMode;
  swapchainInfo.clipped = VK_TRUE;

  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  result = vkCreateSwapchainKHR(vkDevice, &swapchainInfo, nullptr, &swapchain);
  if (result != VK_SUCCESS) {
    LOGE("VulkanWindow: vkCreateSwapchainKHR failed: %s", VkResultToString(result));
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }

  uint32_t swapImageCount = 0;
  vkGetSwapchainImagesKHR(vkDevice, swapchain, &swapImageCount, nullptr);
  std::vector<VkImage> images(swapImageCount);
  vkGetSwapchainImagesKHR(vkDevice, swapchain, &swapImageCount, images.data());

  device->unlock();

  auto imageStates = CreateImageStates(vkDevice, images.size());
  if (images.empty() || imageStates.size() != images.size()) {
    DestroySwapchainResources(vkDevice, vkInstance, surface, swapchain);
    return nullptr;
  }
  auto state = std::make_unique<PlatformState>();
  state->cachedDevice = vkDevice;
  state->cachedInstance = vkInstance;
  state->surface = surface;
  state->swapchain = swapchain;
  state->images = std::move(images);
  state->imageStates = std::move(imageStates);
  state->format = chosenFormat.format;
  state->colorSpace = chosenFormat.colorSpace;
  state->compositeAlpha = compositeAlpha;
  state->presentMode = presentMode;
  state->width = static_cast<int>(extent.width);
  state->height = static_cast<int>(extent.height);
  state->supportsReadback = VulkanSupportsReadback(imageUsage);

  return std::shared_ptr<VulkanWindow>(
      new VulkanWindow(device, std::move(state), colorSpace, vsyncEnabled));
}

#endif

#ifdef __ANDROID__

std::shared_ptr<VulkanWindow> VulkanWindow::MakeFrom(ANativeWindow* nativeWindow,
                                                     std::shared_ptr<VulkanDevice> device,
                                                     std::shared_ptr<ColorSpace> colorSpace,
                                                     bool vsyncEnabled) {
  if (nativeWindow == nullptr || device == nullptr) {
    return nullptr;
  }

  if (colorSpace && !colorSpace->isSRGB()) {
    LOGI(
        "VulkanWindow::MakeFrom(): non-sRGB colorSpace is not yet supported and will be ignored. "
        "Only sRGB output is currently available.");
  }

  auto vulkanGPU = static_cast<VulkanGPU*>(device->lockContext()->gpu());

  if (!vulkanGPU->extensions().swapchain) {
    LOGE("VulkanWindow: swapchain extension not available; cannot create window surface.");
    device->unlock();
    return nullptr;
  }

  auto vkInstance = vulkanGPU->instance();
  auto vkDevice = vulkanGPU->device();
  auto physicalDevice = vulkanGPU->physicalDevice();

  VkAndroidSurfaceCreateInfoKHR surfaceInfo = {};
  surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
  surfaceInfo.window = nativeWindow;

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  auto result = vkCreateAndroidSurfaceKHR(vkInstance, &surfaceInfo, nullptr, &surface);
  if (result != VK_SUCCESS) {
    LOGE("VulkanWindow: vkCreateAndroidSurfaceKHR failed: %s", VkResultToString(result));
    device->unlock();
    return nullptr;
  }

  VkBool32 presentSupport = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, vulkanGPU->graphicsQueueIndex(), surface,
                                       &presentSupport);
  if (!presentSupport) {
    LOGE("VulkanWindow: graphics queue does not support presentation.");
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }

  VkSurfaceCapabilitiesKHR capabilities = {};
  result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
  if (result != VK_SUCCESS) {
    LOGE("VulkanWindow: vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed: %s",
         VkResultToString(result));
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }

  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
  if (formatCount == 0) {
    LOGE("VulkanWindow: no surface formats available.");
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

  VkSurfaceFormatKHR chosenFormat = formats[0];
  if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
    chosenFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  } else {
    for (auto& f : formats) {
      if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
          f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        chosenFormat = f;
        break;
      }
    }
  }

  VkExtent2D extent = capabilities.currentExtent;
  if (extent.width == 0xFFFFFFFF) {
    extent.width = static_cast<uint32_t>(ANativeWindow_getWidth(nativeWindow));
    extent.height = static_cast<uint32_t>(ANativeWindow_getHeight(nativeWindow));
  }

  uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
  }

  VkImageUsageFlags desiredUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  VkImageUsageFlags imageUsage = desiredUsage & capabilities.supportedUsageFlags;
  if (!(imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
    LOGE("VulkanWindow: surface does not support COLOR_ATTACHMENT usage.");
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }

  VkSwapchainCreateInfoKHR swapchainInfo = {};
  swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainInfo.surface = surface;
  swapchainInfo.minImageCount = imageCount;
  swapchainInfo.imageFormat = chosenFormat.format;
  swapchainInfo.imageColorSpace = chosenFormat.colorSpace;
  swapchainInfo.imageExtent = extent;
  swapchainInfo.imageArrayLayers = 1;
  swapchainInfo.imageUsage = imageUsage;
  swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchainInfo.preTransform = capabilities.currentTransform;
  VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  if (!(capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)) {
    compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  }
  swapchainInfo.compositeAlpha = compositeAlpha;
  auto presentMode = ChoosePresentMode(physicalDevice, surface, vsyncEnabled);
  swapchainInfo.presentMode = presentMode;
  swapchainInfo.clipped = VK_TRUE;

  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  result = vkCreateSwapchainKHR(vkDevice, &swapchainInfo, nullptr, &swapchain);
  if (result != VK_SUCCESS) {
    LOGE("VulkanWindow: vkCreateSwapchainKHR failed: %s", VkResultToString(result));
    vkDestroySurfaceKHR(vkInstance, surface, nullptr);
    device->unlock();
    return nullptr;
  }

  uint32_t swapImageCount = 0;
  vkGetSwapchainImagesKHR(vkDevice, swapchain, &swapImageCount, nullptr);
  std::vector<VkImage> images(swapImageCount);
  vkGetSwapchainImagesKHR(vkDevice, swapchain, &swapImageCount, images.data());

  device->unlock();

  auto imageStates = CreateImageStates(vkDevice, images.size());
  if (images.empty() || imageStates.size() != images.size()) {
    DestroySwapchainResources(vkDevice, vkInstance, surface, swapchain);
    return nullptr;
  }
  auto state = std::make_unique<PlatformState>();
  state->cachedDevice = vkDevice;
  state->cachedInstance = vkInstance;
  state->surface = surface;
  state->swapchain = swapchain;
  state->images = std::move(images);
  state->imageStates = std::move(imageStates);
  state->format = chosenFormat.format;
  state->colorSpace = chosenFormat.colorSpace;
  state->compositeAlpha = compositeAlpha;
  state->presentMode = presentMode;
  state->width = static_cast<int>(extent.width);
  state->height = static_cast<int>(extent.height);
  state->supportsReadback = VulkanSupportsReadback(imageUsage);

  return std::shared_ptr<VulkanWindow>(
      new VulkanWindow(device, std::move(state), colorSpace, vsyncEnabled));
}

#endif

VulkanWindow::VulkanWindow(std::shared_ptr<Device> device, std::unique_ptr<PlatformState> state,
                           std::shared_ptr<ColorSpace> colorSpace, bool vsyncEnabled)
    : Window(std::move(device), std::move(colorSpace), vsyncEnabled),
      _platformState(std::move(state)) {
}

VulkanWindow::~VulkanWindow() {
  auto context = device->lockContext();
  if (context != nullptr) {
    auto vulkanGPU = static_cast<VulkanGPU*>(context->gpu());
    auto vkDevice = vulkanGPU->device();
    // Ensure all in-flight submissions referencing swapchain images have completed before
    // destroying the swapchain and its image views.
    vkDeviceWaitIdle(vkDevice);
    DestroyImageStates(vkDevice, _platformState->imageStates);
    DestroySwapchainResources(vkDevice, vulkanGPU->instance(), _platformState->surface,
                              _platformState->swapchain);
    device->unlock();
  } else {
    vkDeviceWaitIdle(_platformState->cachedDevice);
    DestroyImageStates(_platformState->cachedDevice, _platformState->imageStates);
    DestroySwapchainResources(_platformState->cachedDevice, _platformState->cachedInstance,
                              _platformState->surface, _platformState->swapchain);
  }
}

bool VulkanWindow::PlatformState::recreateSwapchain(VkDevice device,
                                                    const VkSurfaceCapabilitiesKHR& capabilities,
                                                    const VkExtent2D& extent) {
  vkDeviceWaitIdle(device);

  uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
  }
  VkImageUsageFlags desiredUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  VkImageUsageFlags imageUsage = desiredUsage & capabilities.supportedUsageFlags;
  if (!(imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
    return false;
  }

  VkSwapchainCreateInfoKHR swapchainInfo = {};
  swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainInfo.surface = surface;
  swapchainInfo.minImageCount = imageCount;
  swapchainInfo.imageFormat = format;
  swapchainInfo.imageColorSpace = colorSpace;
  swapchainInfo.imageExtent = extent;
  swapchainInfo.imageArrayLayers = 1;
  swapchainInfo.imageUsage = imageUsage;
  swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchainInfo.preTransform = capabilities.currentTransform;
  swapchainInfo.compositeAlpha = compositeAlpha;
  swapchainInfo.presentMode = presentMode;
  swapchainInfo.clipped = VK_TRUE;
  swapchainInfo.oldSwapchain = swapchain;

  VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
  if (vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &newSwapchain) != VK_SUCCESS) {
    return false;
  }
  uint32_t newImageCount = 0;
  if (vkGetSwapchainImagesKHR(device, newSwapchain, &newImageCount, nullptr) != VK_SUCCESS ||
      newImageCount == 0) {
    vkDestroySwapchainKHR(device, newSwapchain, nullptr);
    return false;
  }
  std::vector<VkImage> newImages(newImageCount);
  if (vkGetSwapchainImagesKHR(device, newSwapchain, &newImageCount, newImages.data()) !=
      VK_SUCCESS) {
    vkDestroySwapchainKHR(device, newSwapchain, nullptr);
    return false;
  }
  auto newImageStates = CreateImageStates(device, newImages.size());
  if (newImageStates.size() != newImages.size()) {
    vkDestroySwapchainKHR(device, newSwapchain, nullptr);
    return false;
  }

  DestroyImageStates(device, imageStates);
  vkDestroySwapchainKHR(device, swapchain, nullptr);
  swapchain = newSwapchain;
  images = std::move(newImages);
  imageStates = std::move(newImageStates);
  width = static_cast<int>(extent.width);
  height = static_cast<int>(extent.height);
  supportsReadback = VulkanSupportsReadback(imageUsage);
  outOfDate = std::make_shared<bool>(false);
  // Invalidate all proxies created against the previous swapchain so they never touch the
  // destroyed handles; callers must create a new Surface after a rebuild.
  (*generation)++;
  return true;
}

std::shared_ptr<RenderTargetProxy> VulkanWindow::createSwapchainProxy(Context* context,
                                                                      bool manualPresent) {
  auto vulkanGPU = static_cast<VulkanGPU*>(context->gpu());
  auto vkDevice = vulkanGPU->device();
  auto physicalDevice = vulkanGPU->physicalDevice();

  // Query current surface extent to detect resize.
  VkSurfaceCapabilitiesKHR capabilities = {};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, _platformState->surface, &capabilities);
  auto extent = capabilities.currentExtent;
  if (extent.width == 0xFFFFFFFF) {
    // The surface does not report a fixed extent (e.g. Wayland). Skip this frame and let the
    // next createSwapchainProxy call re-query the capabilities for an updated extent.
    return nullptr;
  }

  bool needsRebuild = (_platformState->swapchain == VK_NULL_HANDLE) || *_platformState->outOfDate ||
                      (static_cast<int>(extent.width) != _platformState->width) ||
                      (static_cast<int>(extent.height) != _platformState->height);
  if (needsRebuild) {
    auto& proxies = _platformState->activeProxies;
    bool hasPendingFrame = false;
    for (auto iterator = proxies.begin(); iterator != proxies.end();) {
      if (auto proxy = iterator->lock()) {
        hasPendingFrame = hasPendingFrame || proxy->hasPendingFrame();
        ++iterator;
      } else {
        iterator = proxies.erase(iterator);
      }
    }
    // A rebuild destroys the old swapchain, so it can only run while no proxy holds an acquired
    // image. Idle proxies are safe: they detect the generation change and stop acquiring.
    if (extent.width == 0 || extent.height == 0 || _platformState->manualToken->isActive() ||
        hasPendingFrame) {
      return nullptr;
    }
    if (!_platformState->recreateSwapchain(vkDevice, capabilities, extent)) {
      return nullptr;
    }
  }

  std::shared_ptr<VulkanManualToken> token = nullptr;
  if (manualPresent) {
    if (!_platformState->supportsReadback || !_platformState->manualToken->acquire()) {
      return nullptr;
    }
    token = _platformState->manualToken;
  }
  auto proxy = std::make_shared<VulkanSwapchainProxy>(
      context, vulkanGPU, _platformState->swapchain, _platformState->format, _platformState->width,
      _platformState->height, _platformState->images, _platformState->imageStates,
      _platformState->outOfDate, std::move(token), _platformState->generation, manualPresent);
  _platformState->activeProxies.push_back(proxy);
  return proxy;
}

std::shared_ptr<RenderTargetProxy> VulkanWindow::onCreateRenderTarget(Context* context) {
  return createSwapchainProxy(context, false);
}

void VulkanWindow::onPresent(Context*,
                             const std::vector<std::shared_ptr<RenderTargetProxy>>& renderTargets) {
  if (renderTargets.empty()) {
    return;
  }
  auto proxy = std::static_pointer_cast<VulkanSwapchainProxy>(renderTargets.front());
  proxy->releaseFrame();
}

bool VulkanWindow::hasIndependentPresentationTargets() const {
  return true;
}

std::shared_ptr<Drawable> VulkanWindow::onNextDrawable(Context* context) {
  // A manual-present proxy is intentionally not stored in PlatformState: the auto presentation
  // path tracks the last proxy for out-of-date detection, while this proxy is owned by the
  // returned drawable for its single-frame lifetime.
  auto proxy = std::static_pointer_cast<VulkanSwapchainProxy>(createSwapchainProxy(context, true));
  if (proxy == nullptr) {
    return nullptr;
  }
  return VulkanDrawable::Make(context, std::move(proxy), colorSpace());
}

}  // namespace tgfx
