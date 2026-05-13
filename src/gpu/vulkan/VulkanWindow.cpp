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
#include <vector>
#include "core/utils/Log.h"
#include "gpu/vulkan/VulkanAPI.h"
#include "gpu/vulkan/VulkanGPU.h"
#include "gpu/vulkan/VulkanSwapchainProxy.h"
#include "gpu/vulkan/VulkanUtil.h"

namespace tgfx {

static void DestroySwapchainResources(VkDevice device, VkInstance instance, VkSurfaceKHR surface,
                                      VkSwapchainKHR swapchain,
                                      const std::vector<VkImageView>& imageViews) {
  for (auto view : imageViews) {
    if (view != VK_NULL_HANDLE) {
      vkDestroyImageView(device, view, nullptr);
    }
  }
  if (swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device, swapchain, nullptr);
  }
  if (surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance, surface, nullptr);
  }
}

// Holds all Vulkan-specific handles and swapchain resources. Defined here (not in the header) so
// that vulkan.h is never required by downstream translation units that include VulkanWindow.h.
struct VulkanWindow::PlatformState {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  std::vector<VkImage> images;
  std::vector<VkImageView> imageViews;
  VkFormat format = VK_FORMAT_UNDEFINED;
  int width = 0;
  int height = 0;
  std::shared_ptr<RenderTargetProxy> swapchainProxy;

  bool recreateSwapchain(VkDevice device, const VkSurfaceCapabilitiesKHR& capabilities,
                         const VkExtent2D& extent);
};

#ifdef _WIN32

std::shared_ptr<VulkanWindow> VulkanWindow::MakeFrom(HWND hwnd, std::shared_ptr<Device> device,
                                                     std::shared_ptr<ColorSpace> colorSpace) {
  if (hwnd == nullptr || device == nullptr) {
    return nullptr;
  }

  auto vulkanGPU = static_cast<VulkanGPU*>(device->lockContext()->gpu());
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
  VkImageUsageFlags desiredUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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
  swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
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

  std::vector<VkImageView> imageViews(swapImageCount);
  for (uint32_t i = 0; i < swapImageCount; i++) {
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = images[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = chosenFormat.format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto viewResult = vkCreateImageView(vkDevice, &viewInfo, nullptr, &imageViews[i]);
    if (viewResult != VK_SUCCESS) {
      LOGE("VulkanWindow: vkCreateImageView failed for image %u: %s", i,
           VkResultToString(viewResult));
      DestroySwapchainResources(vkDevice, vkInstance, surface, swapchain, imageViews);
      device->unlock();
      return nullptr;
    }
  }

  device->unlock();

  auto state = std::make_unique<PlatformState>();
  state->surface = surface;
  state->swapchain = swapchain;
  state->images = std::move(images);
  state->imageViews = std::move(imageViews);
  state->format = chosenFormat.format;
  state->width = static_cast<int>(extent.width);
  state->height = static_cast<int>(extent.height);

  return std::shared_ptr<VulkanWindow>(new VulkanWindow(device, std::move(state), colorSpace));
}

#endif

VulkanWindow::VulkanWindow(std::shared_ptr<Device> device, std::unique_ptr<PlatformState> state,
                           std::shared_ptr<ColorSpace> colorSpace)
    : Window(std::move(device), std::move(colorSpace)), _platformState(std::move(state)) {
}

VulkanWindow::~VulkanWindow() {
  auto vulkanGPU = static_cast<VulkanGPU*>(device->lockContext()->gpu());
  auto vkDevice = vulkanGPU->device();
  // Ensure all in-flight submissions referencing swapchain images have completed before
  // destroying the swapchain and its image views.
  vkDeviceWaitIdle(vkDevice);

  DestroySwapchainResources(vkDevice, vulkanGPU->instance(), _platformState->surface,
                            _platformState->swapchain, _platformState->imageViews);
  device->unlock();
}

bool VulkanWindow::PlatformState::recreateSwapchain(VkDevice device,
                                                    const VkSurfaceCapabilitiesKHR& capabilities,
                                                    const VkExtent2D& extent) {
  vkDeviceWaitIdle(device);

  uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
  }

  VkImageUsageFlags desiredUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  VkImageUsageFlags imageUsage = desiredUsage & capabilities.supportedUsageFlags;
  if (!(imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
    LOGE("VulkanWindow: surface does not support COLOR_ATTACHMENT usage.");
    return false;
  }

  VkSwapchainCreateInfoKHR swapchainInfo = {};
  swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainInfo.surface = surface;
  swapchainInfo.minImageCount = imageCount;
  swapchainInfo.imageFormat = format;
  swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  swapchainInfo.imageExtent = extent;
  swapchainInfo.imageArrayLayers = 1;
  swapchainInfo.imageUsage = imageUsage;
  swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchainInfo.preTransform = capabilities.currentTransform;
  swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  swapchainInfo.clipped = VK_TRUE;
  swapchainInfo.oldSwapchain = swapchain;

  VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
  auto result = vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &newSwapchain);
  if (result != VK_SUCCESS) {
    LOGE("VulkanWindow: swapchain rebuild failed: %s", VkResultToString(result));
    return false;
  }

  // Destroy old imageViews and swapchain.
  for (auto view : imageViews) {
    if (view != VK_NULL_HANDLE) {
      vkDestroyImageView(device, view, nullptr);
    }
  }
  if (swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device, swapchain, nullptr);
  }
  swapchain = newSwapchain;

  uint32_t swapImageCount = 0;
  vkGetSwapchainImagesKHR(device, newSwapchain, &swapImageCount, nullptr);
  images.resize(swapImageCount);
  vkGetSwapchainImagesKHR(device, newSwapchain, &swapImageCount, images.data());

  // Recreate imageViews for the new swapchain images.
  imageViews.resize(swapImageCount);
  for (uint32_t i = 0; i < swapImageCount; i++) {
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = images[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto viewResult = vkCreateImageView(device, &viewInfo, nullptr, &imageViews[i]);
    if (viewResult != VK_SUCCESS) {
      LOGE("VulkanWindow: vkCreateImageView failed during rebuild for image %u: %s", i,
           VkResultToString(viewResult));
      return false;
    }
  }

  width = static_cast<int>(extent.width);
  height = static_cast<int>(extent.height);
  return true;
}

std::shared_ptr<RenderTargetProxy> VulkanWindow::onCreateRenderTarget(Context* context) {
  auto vulkanGPU = static_cast<VulkanGPU*>(context->gpu());
  auto vkDevice = vulkanGPU->device();
  auto physicalDevice = vulkanGPU->physicalDevice();

  // Query current surface extent to detect resize.
  VkSurfaceCapabilitiesKHR capabilities = {};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, _platformState->surface, &capabilities);
  auto extent = capabilities.currentExtent;
  if (extent.width == 0xFFFFFFFF) {
    extent.width = static_cast<uint32_t>(_platformState->width);
    extent.height = static_cast<uint32_t>(_platformState->height);
  }

  auto lastProxy =
      std::dynamic_pointer_cast<VulkanSwapchainProxy>(_platformState->swapchainProxy);
  bool needsRebuild = (_platformState->swapchain == VK_NULL_HANDLE) ||
                      (lastProxy && lastProxy->isOutOfDate()) ||
                      (static_cast<int>(extent.width) != _platformState->width) ||
                      (static_cast<int>(extent.height) != _platformState->height);

  if (needsRebuild) {
    if (extent.width == 0 || extent.height == 0) {
      return nullptr;
    }
    // Release the old proxy before rebuilding the swapchain, since recreateSwapchain() destroys
    // the old swapchain images that the proxy references.
    _platformState->swapchainProxy.reset();
    if (!_platformState->recreateSwapchain(vkDevice, capabilities, extent)) {
      return nullptr;
    }
  }

  _platformState->swapchainProxy = std::make_shared<VulkanSwapchainProxy>(
      context, vulkanGPU, _platformState->swapchain, _platformState->format, _platformState->width,
      _platformState->height, _platformState->imageViews, _platformState->images,
      vulkanGPU->acquirePresentationSlot());
  return _platformState->swapchainProxy;
}

void VulkanWindow::onPresent(Context*) {
  if (!_platformState->swapchainProxy) {
    return;
  }
  auto proxy = std::static_pointer_cast<VulkanSwapchainProxy>(_platformState->swapchainProxy);
  proxy->releaseFrame();
}

}  // namespace tgfx
