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
                       std::shared_ptr<VulkanManualToken> manualToken,
                       std::shared_ptr<uint64_t> swapchainGeneration, bool manualPresent = false);
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

  void releaseFrame();
  void presentFrame();

 private:
  void releaseManualToken();

  Context* _context = nullptr;
  VulkanGPU* _gpu = nullptr;
  VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
  VkFormat _format = VK_FORMAT_UNDEFINED;
  int _width = 0;
  int _height = 0;
  std::vector<VkImage> _images;
  std::vector<std::shared_ptr<VulkanSwapchainImageState>> _imageStates;
  std::shared_ptr<bool> _outOfDate;
  std::shared_ptr<VulkanManualToken> _manualToken;
  std::shared_ptr<uint64_t> _swapchainGeneration;
  uint64_t _generationValue = 0;

  mutable uint32_t _currentImageIndex = 0;
  mutable std::shared_ptr<RenderTarget> _renderTarget = nullptr;
  mutable std::shared_ptr<VulkanFrameState> _frameState = nullptr;
  bool _manualPresent = false;
  bool _presentationRequested = false;
};

}  // namespace tgfx
