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
    std::shared_ptr<bool> outOfDate, std::shared_ptr<uint64_t> swapchainGeneration)
    : _context(context), _gpu(gpu), _swapchain(swapchain), _format(format), _width(width),
      _height(height), _images(images), _imageStates(imageStates), _outOfDate(std::move(outOfDate)),
      _swapchainGeneration(std::move(swapchainGeneration)),
      _generationValue(*_swapchainGeneration) {
}

VulkanSwapchainProxy::~VulkanSwapchainProxy() {
  if (_renderTarget != nullptr && _outOfDate != nullptr) {
    // An acquired image can only be reclaimed by presenting it, so rebuild the swapchain.
    *_outOfDate = true;
  }
  if (_pendingImageAvailableSemaphore != VK_NULL_HANDLE) {
    vkDestroySemaphore(_gpu->device(), _pendingImageAvailableSemaphore, nullptr);
    _pendingImageAvailableSemaphore = VK_NULL_HANDLE;
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
  auto window = _presentingWindow.lock();
  if (window == nullptr) {
    // The window is gone and its swapchain handles are dangling.
    return nullptr;
  }
  if (_renderTarget != nullptr) {
    return _renderTarget;
  }
  if (*_swapchainGeneration != _generationValue) {
    // The swapchain was rebuilt; a new Surface is required.
    return nullptr;
  }
  VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
  VkSemaphoreCreateInfo semaphoreInfo = {};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  if (vkCreateSemaphore(_gpu->device(), &semaphoreInfo, nullptr, &imageAvailableSemaphore) !=
      VK_SUCCESS) {
    *_outOfDate = true;
    return nullptr;
  }
  auto result = vkAcquireNextImageKHR(_gpu->device(), _swapchain, UINT64_MAX,
                                      imageAvailableSemaphore, VK_NULL_HANDLE, &_currentImageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    *_outOfDate = true;
  }
  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    vkDestroySemaphore(_gpu->device(), imageAvailableSemaphore, nullptr);
    return nullptr;
  }

  auto& imageState = _imageStates[_currentImageIndex];
  auto texture = VulkanTexture::MakeFrom(_gpu, _images[_currentImageIndex], _format, _width,
                                         _height, TextureUsage::RENDER_ATTACHMENT, false,
                                         *imageState->layout, imageState->layout);
  if (texture == nullptr) {
    vkDeviceWaitIdle(_gpu->device());
    vkDestroySemaphore(_gpu->device(), imageAvailableSemaphore, nullptr);
    *_outOfDate = true;
    return nullptr;
  }
  _renderTarget =
      TextureRenderTarget::MakeFrom(_context, std::move(texture), 1, ImageOrigin::TopLeft, true);
  if (_renderTarget == nullptr) {
    vkDeviceWaitIdle(_gpu->device());
    vkDestroySemaphore(_gpu->device(), imageAvailableSemaphore, nullptr);
    *_outOfDate = true;
    return nullptr;
  }

  // Assign only after all failure paths so releaseFrame() never destroys it twice.
  _pendingImageAvailableSemaphore = imageAvailableSemaphore;
  return _renderTarget;
}

void VulkanSwapchainProxy::attachAcquireWait() {
  // The first submission using the frame waits for its acquisition.
  if (_renderTarget == nullptr || _pendingImageAvailableSemaphore == VK_NULL_HANDLE) {
    return;
  }
  auto window = _presentingWindow.lock();
  if (window == nullptr) {
    // The window is gone; drop the frame.
    releaseFrame();
    return;
  }
  auto queue = static_cast<VulkanCommandQueue*>(_context->gpu()->queue());
  queue->scheduleAcquireWait(_pendingImageAvailableSemaphore, _outOfDate);
  _pendingImageAvailableSemaphore = VK_NULL_HANDLE;
}

void VulkanSwapchainProxy::onAttachSubmission(Context*) {
  // Using the frame without presenting it only waits for the acquisition.
  attachAcquireWait();
}

void VulkanSwapchainProxy::onSchedulePresentation(Context*) {
  attachAcquireWait();
  if (_renderTarget == nullptr) {
    return;
  }
  auto window = _presentingWindow.lock();
  if (window == nullptr) {
    releaseFrame();
    return;
  }
  auto& imageState = _imageStates[_currentImageIndex];
  auto queue = static_cast<VulkanCommandQueue*>(_context->gpu()->queue());
  queue->schedulePresent(_swapchain, _currentImageIndex, _images[_currentImageIndex],
                         imageState->presentSemaphore, imageState->layout, _outOfDate);
}

void VulkanSwapchainProxy::onPresentFrame(Context*) {
  // End the presented frame so the next use acquires a new image.
  releaseFrame();
}

bool VulkanSwapchainProxy::hasPendingFrame() const {
  return _renderTarget != nullptr;
}

void VulkanSwapchainProxy::releaseFrame() {
  if (_pendingImageAvailableSemaphore != VK_NULL_HANDLE) {
    vkDestroySemaphore(_gpu->device(), _pendingImageAvailableSemaphore, nullptr);
    _pendingImageAvailableSemaphore = VK_NULL_HANDLE;
  }
  _renderTarget = nullptr;
}

}  // namespace tgfx
