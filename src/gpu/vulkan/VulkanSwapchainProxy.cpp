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

#include "VulkanSwapchainProxy.h"
#include "VulkanGPU.h"
#include "VulkanUtil.h"
#include "core/utils/Log.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/Backend.h"

namespace tgfx {

VulkanSwapchainProxy::VulkanSwapchainProxy(Context* context, VulkanGPU* gpu,
                                           VkSwapchainKHR swapchain, VkFormat format, int width,
                                           int height, const std::vector<VkImageView>& imageViews,
                                           const std::vector<VkImage>& images)
    : _context(context), _gpu(gpu), _swapchain(swapchain), _format(format), _width(width),
      _height(height), _imageViews(imageViews), _images(images) {
}

VulkanSwapchainProxy::~VulkanSwapchainProxy() {
  if (_acquireFence != VK_NULL_HANDLE) {
    vkDestroyFence(_gpu->device(), _acquireFence, nullptr);
  }
}

Context* VulkanSwapchainProxy::getContext() const {
  return _context;
}

int VulkanSwapchainProxy::width() const {
  return _width;
}

int VulkanSwapchainProxy::height() const {
  return _height;
}

PixelFormat VulkanSwapchainProxy::format() const {
  return VkFormatToPixelFormat(_format);
}

int VulkanSwapchainProxy::sampleCount() const {
  return 1;
}

ImageOrigin VulkanSwapchainProxy::origin() const {
  return ImageOrigin::TopLeft;
}

bool VulkanSwapchainProxy::externallyOwned() const {
  return true;
}

std::shared_ptr<TextureView> VulkanSwapchainProxy::getTextureView() const {
  return nullptr;
}

std::shared_ptr<RenderTarget> VulkanSwapchainProxy::getRenderTarget() const {
  if (_renderTarget == nullptr) {
    if (_acquireFence == VK_NULL_HANDLE) {
      VkFenceCreateInfo fenceInfo = {};
      fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      vkCreateFence(_gpu->device(), &fenceInfo, nullptr, &_acquireFence);
    } else {
      vkResetFences(_gpu->device(), 1, &_acquireFence);
    }
    auto result = vkAcquireNextImageKHR(_gpu->device(), _swapchain, UINT64_MAX, VK_NULL_HANDLE,
                                        _acquireFence, &_currentImageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      // Swapchain is incompatible with the surface (e.g. window resized). The fence may not have
      // been signaled, so attempt a non-blocking wait to consume it before any future reset.
      // TODO: Rebuild swapchain and retry acquire.
      LOGE("VulkanSwapchainProxy: swapchain out of date, needs rebuild.");
      vkWaitForFences(_gpu->device(), 1, &_acquireFence, VK_TRUE, 0);
      return nullptr;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      LOGE("VulkanSwapchainProxy: vkAcquireNextImageKHR failed (result=%d).",
           static_cast<int>(result));
      vkWaitForFences(_gpu->device(), 1, &_acquireFence, VK_TRUE, 0);
      return nullptr;
    }
    vkWaitForFences(_gpu->device(), 1, &_acquireFence, VK_TRUE, UINT64_MAX);
    VulkanImageInfo vulkanInfo = {};
    vulkanInfo.image = reinterpret_cast<void*>(_images[_currentImageIndex]);
    vulkanInfo.format = static_cast<uint32_t>(_format);
    vulkanInfo.layout = static_cast<uint32_t>(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    BackendRenderTarget backendRT(vulkanInfo, _width, _height);
    _renderTarget = RenderTarget::MakeFrom(_context, backendRT, ImageOrigin::TopLeft);
  }
  return _renderTarget;
}

void VulkanSwapchainProxy::releaseFrame() {
  _renderTarget = nullptr;
}

}  // namespace tgfx
