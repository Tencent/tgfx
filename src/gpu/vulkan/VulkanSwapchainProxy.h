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
#include "VulkanPresentationState.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/vulkan/VulkanGPU.h"

namespace tgfx {

class VulkanSwapchainProxy : public RenderTargetProxy {
 public:
  VulkanSwapchainProxy(Context* context, VulkanGPU* gpu, VkSwapchainKHR swapchain, VkFormat format,
                       int width, int height, const std::vector<VkImage>& images,
                       const std::vector<std::shared_ptr<VulkanSwapchainImageState>>& imageStates,
                       std::shared_ptr<bool> outOfDate,
                       std::shared_ptr<uint64_t> swapchainGeneration);
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

  /**
   * Returns true if this proxy has acquired a swapchain image that has not been presented yet.
   */
  bool hasPendingFrame() const;

  /**
   * Hands the acquired frame's acquire semaphore to the upcoming submission, so that submission
   * waits for the image acquisition before touching the image. Done once per frame, by the first
   * submission that uses it; never presents the image.
   */
  void attachAcquireWait();

  void releaseFrame();

  /**
   * Frame-level wiring, independent of the VulkanWindow: makes a batch that uses the frame wait
   * for its acquisition, whether or not a presentation was registered for that batch. The frame
   * stays open; it is only presented by the batch carrying the presentation request.
   */
  void onAttachSubmission(Context* context) override;

  /**
   * Queues the frame's presentation with the submission of the batch that draws it (wiring the
   * acquire wait too if that batch is the frame's first use).
   */
  void onSchedulePresentation(Context* context) override;

  /**
   * Ends the frame after the submission that presented it, so the next use acquires a fresh
   * image.
   */
  void onPresentFrame(Context* context) override;

 private:
  Context* _context = nullptr;
  VulkanGPU* _gpu = nullptr;
  VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
  VkFormat _format = VK_FORMAT_UNDEFINED;
  int _width = 0;
  int _height = 0;
  std::vector<VkImage> _images;
  std::vector<std::shared_ptr<VulkanSwapchainImageState>> _imageStates;
  std::shared_ptr<bool> _outOfDate;
  std::shared_ptr<uint64_t> _swapchainGeneration;
  uint64_t _generationValue = 0;

  mutable uint32_t _currentImageIndex = 0;
  mutable std::shared_ptr<RenderTarget> _renderTarget = nullptr;
  // The acquire semaphore of the pending frame, created per acquisition and handed to the
  // command queue in attachAcquireWait(); destroyed with the in-flight submission's fence.
  mutable VkSemaphore _pendingImageAvailableSemaphore = VK_NULL_HANDLE;
};

}  // namespace tgfx
