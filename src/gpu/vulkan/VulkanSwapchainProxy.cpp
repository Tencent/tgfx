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
#include "VulkanCommandQueue.h"
#include "VulkanTexture.h"
#include "VulkanUtil.h"
#include "core/utils/Log.h"
#include "gpu/resources/TextureRenderTarget.h"

namespace tgfx {

VulkanSwapchainProxy::VulkanSwapchainProxy(
    Context* context, VulkanGPU* gpu, VkSwapchainKHR swapchain, VkFormat format, int width,
    int height, const std::vector<VkImage>& images,
    const std::vector<std::shared_ptr<VulkanSwapchainImageState>>& imageStates,
    std::shared_ptr<bool> outOfDate, std::shared_ptr<VulkanManualToken> manualToken,
    bool manualPresent)
    : _context(context), _gpu(gpu), _swapchain(swapchain), _format(format), _width(width),
      _height(height), _images(images), _imageStates(imageStates), _outOfDate(std::move(outOfDate)),
      _manualToken(std::move(manualToken)), _manualPresent(manualPresent) {
}

VulkanSwapchainProxy::~VulkanSwapchainProxy() {
  if (_imageAvailableSemaphore != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(_gpu->device());
    vkDestroySemaphore(_gpu->device(), _imageAvailableSemaphore, nullptr);
  }
  releaseManualToken();
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
  if (_renderTarget != nullptr) {
    return _renderTarget;
  }
  if (_frameState != nullptr || _presentationRequested) {
    return nullptr;
  }

  VkSemaphoreCreateInfo semaphoreInfo = {};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  if (vkCreateSemaphore(_gpu->device(), &semaphoreInfo, nullptr, &_imageAvailableSemaphore) !=
      VK_SUCCESS) {
    *_outOfDate = true;
    return nullptr;
  }
  auto result =
      vkAcquireNextImageKHR(_gpu->device(), _swapchain, UINT64_MAX, _imageAvailableSemaphore,
                            VK_NULL_HANDLE, &_currentImageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    *_outOfDate = true;
  }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    vkDestroySemaphore(_gpu->device(), _imageAvailableSemaphore, nullptr);
    _imageAvailableSemaphore = VK_NULL_HANDLE;
    return nullptr;
  }

  auto& imageState = _imageStates[_currentImageIndex];
  auto texture =
      VulkanTexture::MakeFrom(_gpu, _images[_currentImageIndex], _format, _width, _height,
                              TextureUsage::TEXTURE_BINDING | TextureUsage::RENDER_ATTACHMENT,
                              false, *imageState->layout, imageState->layout);
  if (texture == nullptr) {
    vkDeviceWaitIdle(_gpu->device());
    vkDestroySemaphore(_gpu->device(), _imageAvailableSemaphore, nullptr);
    _imageAvailableSemaphore = VK_NULL_HANDLE;
    *_outOfDate = true;
    return nullptr;
  }
  _renderTarget =
      TextureRenderTarget::MakeFrom(_context, std::move(texture), 1, ImageOrigin::TopLeft, true);
  if (_renderTarget == nullptr) {
    vkDeviceWaitIdle(_gpu->device());
    vkDestroySemaphore(_gpu->device(), _imageAvailableSemaphore, nullptr);
    _imageAvailableSemaphore = VK_NULL_HANDLE;
    *_outOfDate = true;
    return nullptr;
  }

  _frameState = std::make_shared<VulkanFrameState>(_manualPresent);
  auto queue = static_cast<VulkanCommandQueue*>(_context->gpu()->queue());
  queue->schedulePresent(_swapchain, _currentImageIndex, _images[_currentImageIndex],
                         _imageAvailableSemaphore, imageState->presentSemaphore, imageState->layout,
                         _outOfDate, _frameState, _manualPresent);
  _imageAvailableSemaphore = VK_NULL_HANDLE;
  return _renderTarget;
}

void VulkanSwapchainProxy::releaseFrame() {
  _renderTarget = nullptr;
}

void VulkanSwapchainProxy::presentFrame() {
  _presentationRequested = true;
  if (!_manualPresent || _frameState == nullptr || !_frameState->beginPresent()) {
    releaseManualToken();
    return;
  }
  auto& imageState = _imageStates[_currentImageIndex];
  _gpu->presentNow(_swapchain, _currentImageIndex, _images[_currentImageIndex],
                   imageState->presentSemaphore, imageState->layout, _outOfDate);
  releaseFrame();
  releaseManualToken();
}

void VulkanSwapchainProxy::releaseManualToken() {
  if (_manualToken != nullptr) {
    _manualToken->release();
    _manualToken = nullptr;
  }
}

}  // namespace tgfx
