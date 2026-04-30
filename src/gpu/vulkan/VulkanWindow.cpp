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

#include "gpu/vulkan/VulkanAPI.h"
#include "tgfx/gpu/vulkan/VulkanWindow.h"
#include <vector>
#include "core/utils/Log.h"
#include "gpu/vulkan/VulkanGPU.h"
#include "gpu/vulkan/VulkanSwapchainProxy.h"
#include "gpu/vulkan/VulkanUtil.h"

namespace tgfx {

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
  auto* images = new std::vector<VkImage>(swapImageCount);
  vkGetSwapchainImagesKHR(vkDevice, swapchain, &swapImageCount, images->data());

  auto* imageViews = new std::vector<VkImageView>(swapImageCount);
  for (uint32_t i = 0; i < swapImageCount; i++) {
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = (*images)[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = chosenFormat.format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(vkDevice, &viewInfo, nullptr, &(*imageViews)[i]);
  }

  device->unlock();

  auto window = std::shared_ptr<VulkanWindow>(
      new VulkanWindow(device, reinterpret_cast<void*>(surface), reinterpret_cast<void*>(swapchain),
                       reinterpret_cast<void*>(imageViews), reinterpret_cast<void*>(images),
                       static_cast<unsigned>(chosenFormat.format), static_cast<int>(extent.width),
                       static_cast<int>(extent.height), static_cast<int>(swapImageCount)));
  return window;
}

#endif

VulkanWindow::VulkanWindow(std::shared_ptr<Device> device, void* surface, void* swapchain,
                           void* imageViews, void* images, unsigned format, int width, int height,
                           int imageCount)
    : Window(std::move(device)), _surface(surface), _swapchain(swapchain), _imageViews(imageViews),
      _images(images), _format(format), _width(width), _height(height), _imageCount(imageCount) {
}

VulkanWindow::~VulkanWindow() {
  auto vulkanGPU = static_cast<VulkanGPU*>(device->lockContext()->gpu());
  auto vkDevice = vulkanGPU->device();

  auto* imageViews = reinterpret_cast<std::vector<VkImageView>*>(_imageViews);
  if (imageViews) {
    for (auto& view : *imageViews) {
      vkDestroyImageView(vkDevice, view, nullptr);
    }
    delete imageViews;
  }
  auto* images = reinterpret_cast<std::vector<VkImage>*>(_images);
  delete images;

  auto swapchain = reinterpret_cast<VkSwapchainKHR>(_swapchain);
  if (swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(vkDevice, swapchain, nullptr);
  }
  auto surface = reinterpret_cast<VkSurfaceKHR>(_surface);
  if (surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(vulkanGPU->instance(), surface, nullptr);
  }
  device->unlock();
}

std::shared_ptr<RenderTargetProxy> VulkanWindow::onCreateRenderTarget(Context* context) {
  auto vulkanGPU = static_cast<VulkanGPU*>(context->gpu());
  auto swapchain = reinterpret_cast<VkSwapchainKHR>(_swapchain);
  auto* imageViews = reinterpret_cast<std::vector<VkImageView>*>(_imageViews);
  auto* images = reinterpret_cast<std::vector<VkImage>*>(_images);

  swapchainProxy = std::make_shared<VulkanSwapchainProxy>(context, vulkanGPU, swapchain,
                                                          static_cast<VkFormat>(_format), _width,
                                                          _height, *imageViews, *images);
  return swapchainProxy;
}

void VulkanWindow::onPresent(Context* context) {
  if (swapchainProxy == nullptr) {
    return;
  }
  auto proxy = std::static_pointer_cast<VulkanSwapchainProxy>(swapchainProxy);
  auto vulkanGPU = static_cast<VulkanGPU*>(context->gpu());
  auto swapchain = reinterpret_cast<VkSwapchainKHR>(_swapchain);
  uint32_t imageIndex = proxy->currentImageIndex();

  VkPresentInfoKHR presentInfo = {};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &swapchain;
  presentInfo.pImageIndices = &imageIndex;

  vkQueuePresentKHR(vulkanGPU->graphicsQueue(), &presentInfo);
  proxy->releaseFrame();
}

}  // namespace tgfx
