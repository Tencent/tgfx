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
#include "gpu/vulkan/VulkanGPU.h"

namespace tgfx {

/**
 * Manages swapchain image acquisition and per-frame presentation for window rendering.
 *
 * Synchronization model:
 *   - backpressure: VulkanGPU::acquirePresentationSlot() blocks if MAX_FRAMES_IN_FLIGHT
 *     submissions are in flight, ensuring the returned semaphore pair is safe to reuse.
 *   - acquire: vkAcquireNextImageKHR signals imageAvailable semaphore. CPU returns immediately
 *     without blocking; the GPU waits on it at COLOR_ATTACHMENT_OUTPUT before rendering.
 *   - submit: waits on imageAvailable, signals renderFinished after rendering completes.
 *   - present: scheduled via VulkanCommandQueue::schedulePresent() during acquire. The queue
 *     appends a GENERAL to PRESENT_SRC layout transition to the render batch and calls
 *     vkQueuePresentKHR after submit, waiting on renderFinished.
 */
class VulkanSwapchainProxy : public RenderTargetProxy {
 public:
  VulkanSwapchainProxy(Context* context, VulkanGPU* gpu, VkSwapchainKHR swapchain, VkFormat format,
                       int width, int height, const std::vector<VkImageView>& imageViews,
                       const std::vector<VkImage>& images,
                       const VulkanGPU::PresentationSlot& slot);
  ~VulkanSwapchainProxy() override = default;

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

  VkSemaphore _imageAvailableSemaphore = VK_NULL_HANDLE;
  VkSemaphore _renderFinishedSemaphore = VK_NULL_HANDLE;
  mutable uint32_t _currentImageIndex = 0;
  mutable std::shared_ptr<RenderTarget> _renderTarget = nullptr;
};

}  // namespace tgfx
