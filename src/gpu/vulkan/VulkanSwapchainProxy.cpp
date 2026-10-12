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
    std::shared_ptr<uint64_t> swapchainGeneration, bool manualPresent)
    : _context(context), _gpu(gpu), _swapchain(swapchain), _format(format), _width(width),
      _height(height), _images(images), _imageStates(imageStates), _outOfDate(std::move(outOfDate)),
      _manualToken(std::move(manualToken)), _swapchainGeneration(std::move(swapchainGeneration)),
      _generationValue(*_swapchainGeneration), _manualPresent(manualPresent) {
}

VulkanSwapchainProxy::~VulkanSwapchainProxy() {
  if (_renderTarget != nullptr && _outOfDate != nullptr) {
    // The frame was acquired but never presented (for example its surface was dropped after a
    // readback-only batch). An acquired image is only returned to the swapchain by presenting
    // it, so flag the swapchain for a rebuild that reclaims the image.
    *_outOfDate = true;
  }
  if (_pendingImageAvailableSemaphore != VK_NULL_HANDLE) {
    // The frame was acquired but never scheduled; destroy the unused semaphore.
    vkDestroySemaphore(_gpu->device(), _pendingImageAvailableSemaphore, nullptr);
    _pendingImageAvailableSemaphore = VK_NULL_HANDLE;
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
  auto window = _presentingWindow.lock();
  if (window == nullptr) {
    // The window that owns the swapchain is gone; its handles are dangling, so neither the
    // cached target (it wraps destroyed images) nor a new frame may be served. The window is
    // kept alive below for the duration of the acquisition.
    return nullptr;
  }
  if (_renderTarget != nullptr) {
    return _renderTarget;
  }
  if (*_swapchainGeneration != _generationValue) {
    // The swapchain has been rebuilt since this proxy was created. The cached handles are stale;
    // the caller must create a new Surface, matching the documented resize contract.
    return nullptr;
  }
  if (_frameState != nullptr || _presentationRequested) {
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

  _frameState = std::make_shared<VulkanFrameState>(_manualPresent);
  // Keep the acquire semaphore for the submission-time attachAcquireWait(); it is handed to the
  // command queue there and destroyed with the in-flight submission's fence. Assign the member
  // only after every failure path has passed, so the defensive destroys in releaseFrame()/
  // discardFrame()/the destructor never see an already-destroyed handle.
  _pendingImageAvailableSemaphore = imageAvailableSemaphore;
  return _renderTarget;
}

void VulkanSwapchainProxy::attachAcquireWait() {
  // Hand the acquire semaphore to the first submission that uses the frame, so every command
  // touching the image (rendering, readback transfers, snapshot copies) waits for the
  // acquisition. The semaphore is handed over once; later submissions of the same frame are
  // ordered after this one by the queue. This never presents the image.
  if (_renderTarget == nullptr || _pendingImageAvailableSemaphore == VK_NULL_HANDLE) {
    return;
  }
  auto window = _presentingWindow.lock();
  if (window == nullptr) {
    // The window that owns the swapchain is gone; drop the frame instead of wiring dangling
    // handles. The window is kept alive below for the duration of the wiring.
    releaseFrame();
    return;
  }
  auto queue = static_cast<VulkanCommandQueue*>(_context->gpu()->queue());
  queue->scheduleAcquireWait(_pendingImageAvailableSemaphore, _outOfDate, _frameState);
  _pendingImageAvailableSemaphore = VK_NULL_HANDLE;
}

void VulkanSwapchainProxy::onAttachSubmission(Context*) {
  // A batch that uses the frame without a registered presentation (for example a readback or a
  // snapshot copy) only waits for the acquisition; the frame stays open for later drawing.
  attachAcquireWait();
}

void VulkanSwapchainProxy::onSchedulePresentation(Context*) {
  // The batch carrying the presentation request also needs the acquire wait if it is the
  // frame's first use. Manual (explicit-path) frames defer the present to presentFrame(), so
  // their content stays readable after this submission.
  attachAcquireWait();
  if (_manualPresent || _renderTarget == nullptr) {
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
  if (_manualPresent) {
    presentFrame();
  } else {
    // The present was queued by this batch's submission; end the frame so the next use
    // acquires a fresh image.
    releaseFrame();
  }
}

bool VulkanSwapchainProxy::hasPendingFrame() const {
  return _renderTarget != nullptr;
}

void VulkanSwapchainProxy::releaseFrame() {
  if (_pendingImageAvailableSemaphore != VK_NULL_HANDLE) {
    // The frame was acquired but its presentation was never scheduled; destroy the unused
    // semaphore (it was never waited on, so this is safe).
    vkDestroySemaphore(_gpu->device(), _pendingImageAvailableSemaphore, nullptr);
    _pendingImageAvailableSemaphore = VK_NULL_HANDLE;
  }
  _renderTarget = nullptr;
  // The frame state was consumed by the render submission, so a new frame can be acquired.
  _frameState = nullptr;
}

void VulkanSwapchainProxy::presentFrame() {
  _presentationRequested = true;
  auto window = _presentingWindow.lock();
  if (window == nullptr) {
    // The window that owns the swapchain is gone; drop the pending frame instead of
    // presenting through dangling handles. The window is kept alive below for the duration of
    // the present.
    releaseFrame();
    return;
  }
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

void VulkanSwapchainProxy::discardFrame() {
  if (_outOfDate != nullptr) {
    *_outOfDate = true;
  }
  releaseFrame();
}

void VulkanSwapchainProxy::releaseManualToken() {
  if (_manualToken != nullptr) {
    _manualToken->release();
    _manualToken = nullptr;
  }
}

}  // namespace tgfx
