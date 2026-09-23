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
#include "gpu/vulkan/VulkanSwapchainProxy.h"
#include "tgfx/gpu/Drawable.h"

namespace tgfx {

/**
 * A Drawable backed by a Vulkan swapchain image. Unlike the window's automatic presentation path,
 * which presents the image at the end of the render submission, the presentation is deferred to
 * present(), so readPixels() between the render submission and the presentation reads the image
 * that was just rendered. Reading after present() is not supported on this backend because the
 * presentation engine owns the image once it has been presented.
 */
class VulkanDrawable : public Drawable {
 public:
  static std::shared_ptr<VulkanDrawable> Make(Context* context,
                                              std::shared_ptr<VulkanSwapchainProxy> proxy,
                                              std::shared_ptr<ColorSpace> colorSpace);

  ~VulkanDrawable() override;

 protected:
  void onPresent() override;

 private:
  VulkanDrawable(Context* context, std::shared_ptr<RenderTargetProxy> renderTarget,
                 std::shared_ptr<VulkanSwapchainProxy> proxy,
                 std::shared_ptr<ColorSpace> colorSpace);

  std::shared_ptr<VulkanSwapchainProxy> _proxy = nullptr;
};

}  // namespace tgfx
