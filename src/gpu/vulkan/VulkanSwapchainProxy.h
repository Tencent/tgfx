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

#pragma once

#include <vector>
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/vulkan/VulkanAPI.h"

namespace tgfx {

class VulkanGPU;

/**
 * Manages swapchain image acquisition and per-frame synchronization for window presentation.
 *
 * Synchronization model (replaces the previous vkQueueWaitIdle approach):
 *   - acquire: signals imageAvailableSemaphore (GPU-side, no CPU block)
 *   - render submit: waits imageAvailableSemaphore, signals renderFinishedSemaphore
 *   - present: waits renderFinishedSemaphore
 *   - per-frame fence: CPU backpressure to prevent acquiring more than MAX_FRAMES_IN_FLIGHT
 */
class VulkanSwapchainProxy : public RenderTargetProxy {
 public:
  VulkanSwapchainProxy(Context* context, VulkanGPU* gpu, VkSwapchainKHR swapchain, VkFormat format,
                       int width, int height, const std::vector<VkImageView>& imageViews,
                       const std::vector<VkImage>& images);
  ~VulkanSwapchainProxy() override;

  Context* getContext() const override;
  int width() const override;
  int height() const override;
  PixelFormat format() const override;
  int sampleCount() const override;
  ImageOrigin origin() const override;
  bool externallyOwned() const override;
  std::shared_ptr<TextureView> getTextureView() const override;
  std::shared_ptr<RenderTarget> getRenderTarget() const override;

  uint32_t currentImageIndex() const {
    return _currentImageIndex;
  }

  VkImage currentImage() const {
    return _images[_currentImageIndex];
  }

  /// Returns the semaphore signaled by vkAcquireNextImageKHR. The render submit must wait on this
  /// at VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT before writing to the swapchain image.
  VkSemaphore imageAvailableSemaphore() const {
    return _imageAvailableSemaphore;
  }

  /// Returns the semaphore that the render submit should signal upon completion.
  /// vkQueuePresentKHR waits on this to ensure rendering finishes before presentation.
  VkSemaphore renderFinishedSemaphore() const {
    return _renderFinishedSemaphore;
  }

  /// Returns the fence used for CPU backpressure. The present submit signals this fence;
  /// the next frame's acquire waits on it to prevent overwriting in-flight resources.
  VkFence inFlightFence() const {
    return _inFlightFence;
  }

  void releaseFrame();

 private:
  Context* _context = nullptr;
  VulkanGPU* _gpu = nullptr;
  VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
  VkFormat _format = VK_FORMAT_UNDEFINED;
  int _width = 0;
  int _height = 0;
  std::vector<VkImageView> _imageViews;
  std::vector<VkImage> _images;

  // Per-frame synchronization primitives.
  mutable VkSemaphore _imageAvailableSemaphore = VK_NULL_HANDLE;
  mutable VkSemaphore _renderFinishedSemaphore = VK_NULL_HANDLE;
  mutable VkFence _inFlightFence = VK_NULL_HANDLE;
  mutable uint32_t _currentImageIndex = 0;
  mutable std::shared_ptr<RenderTarget> _renderTarget = nullptr;
};

}  // namespace tgfx
