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

#include <memory>
#include "tgfx/core/ColorSpace.h"
#include "tgfx/gpu/Window.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace tgfx {

class Device;

/**
 * VulkanWindow manages a VkSurfaceKHR and VkSwapchainKHR for presenting rendered content to a
 * platform window.
 */
class VulkanWindow : public Window {
 public:
#ifdef _WIN32
  /**
   * Creates a VulkanWindow from a Win32 window handle. The device must be a VulkanDevice. Returns
   * nullptr if the Vulkan surface or swapchain cannot be created.
   */
  static std::shared_ptr<VulkanWindow> MakeFrom(HWND hwnd, std::shared_ptr<Device> device,
                                                std::shared_ptr<ColorSpace> colorSpace = nullptr);
#endif

  ~VulkanWindow() override;

 protected:
  std::shared_ptr<RenderTargetProxy> onCreateRenderTarget(Context* context) override;
  void onPresent(Context* context) override;

 private:
  // PImpl: all Vulkan handles and swapchain state live in PlatformState (defined in .cpp) to avoid
  // exposing vulkan.h in this public header. This also sidesteps the 32-bit portability issue where
  // non-dispatchable Vulkan handles (uint64_t) cannot safely round-trip through void*.
  struct PlatformState;

  explicit VulkanWindow(std::shared_ptr<Device> device, std::unique_ptr<PlatformState> state,
                        std::shared_ptr<ColorSpace> colorSpace);

  std::unique_ptr<PlatformState> _platformState;
};

}  // namespace tgfx
