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
#include <vector>
#include "core/utils/Log.h"
#include "gpu/vulkan/VulkanAPI.h"
#include "gpu/vulkan/VulkanGPU.h"
#include "gpu/vulkan/VulkanSwapchainProxy.h"
#include "gpu/vulkan/VulkanUtil.h"

namespace tgfx {

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
};

#ifdef _WIN32

std::shared_ptr<VulkanWindow> VulkanWindow::MakeFrom(HWND hwnd, std::shared_ptr<Device> device) {
  if (hwnd == nullptr || device == nullptr) {
    return nullptr;
  }

  auto vulkanGPU = static_cast<VulkanGPU*>(device->lockContext()->gpu());
  auto vkInstance = vulkanGPU->instance();
  auto vkDevice = vulkanGPU->device();
  auto physicalDevice = vulkanGPU->physicalDevice();

  VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
  surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surfaceInfo.hinstance = GetModuleHandle(nullptr);
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
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

  VkSurfaceFormatKHR chosenFormat = formats[0];
  for (auto& f : formats) {
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      chosenFormat = f;
      break;
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

  VkSwapchainCreateInfoKHR swapchainInfo = {};
  swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainInfo.surface = surface;
  swapchainInfo.minImageCount = imageCount;
  swapchainInfo.imageFormat = chosenFormat.format;
  swapchainInfo.imageColorSpace = chosenFormat.colorSpace;
  swapchainInfo.imageExtent = extent;
  swapchainInfo.imageArrayLayers = 1;
  swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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
    vkCreateImageView(vkDevice, &viewInfo, nullptr, &imageViews[i]);
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

  return std::shared_ptr<VulkanWindow>(new VulkanWindow(device, std::move(state)));
}

#endif

VulkanWindow::VulkanWindow(std::shared_ptr<Device> device, std::unique_ptr<PlatformState> state)
    : Window(std::move(device)), _platformState(std::move(state)) {
}

VulkanWindow::~VulkanWindow() {
  auto vulkanGPU = static_cast<VulkanGPU*>(device->lockContext()->gpu());
  auto vkDevice = vulkanGPU->device();

  for (auto view : _platformState->imageViews) {
    vkDestroyImageView(vkDevice, view, nullptr);
  }
  if (_platformState->swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(vkDevice, _platformState->swapchain, nullptr);
  }
  if (_platformState->surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(vulkanGPU->instance(), _platformState->surface, nullptr);
  }
  device->unlock();
}

std::shared_ptr<RenderTargetProxy> VulkanWindow::onCreateRenderTarget(Context* context) {
  auto vulkanGPU = static_cast<VulkanGPU*>(context->gpu());
  _platformState->swapchainProxy = std::make_shared<VulkanSwapchainProxy>(
      context, vulkanGPU, _platformState->swapchain, _platformState->format, _platformState->width,
      _platformState->height, _platformState->imageViews, _platformState->images);
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
