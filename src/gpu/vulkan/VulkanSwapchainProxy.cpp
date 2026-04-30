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
    auto result = vkAcquireNextImageKHR(_gpu->device(), _swapchain, UINT64_MAX, VK_NULL_HANDLE,
                                        VK_NULL_HANDLE, &_currentImageIndex);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      return nullptr;
    }
    VulkanImageInfo vulkanInfo = {};
    vulkanInfo.image = _images[_currentImageIndex];
    vulkanInfo.format = _format;
    vulkanInfo.layout = static_cast<VkImageLayout>(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    BackendRenderTarget backendRT(vulkanInfo, _width, _height);
    _renderTarget = RenderTarget::MakeFrom(_context, backendRT, ImageOrigin::TopLeft);
  }
  return _renderTarget;
}

void VulkanSwapchainProxy::releaseFrame() {
  _renderTarget = nullptr;
}

}  // namespace tgfx
