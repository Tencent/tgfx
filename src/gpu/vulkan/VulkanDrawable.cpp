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

#include "VulkanDrawable.h"
#include "VulkanSwapchainProxy.h"
#include "tgfx/gpu/vulkan/VulkanWindow.h"

namespace tgfx {

std::shared_ptr<VulkanDrawable> VulkanDrawable::Make(VulkanGPU* gpu,
                                                     std::shared_ptr<VulkanWindow> window) {
  if (window == nullptr) {
    return nullptr;
  }
  // The drawable is registered as a Vulkan resource: when its last reference goes away it is
  // returned to the GPU's return queue and destroyed (releasing the manual-present proxy and
  // its semaphores) within the device's synchronized scope instead of on an arbitrary thread.
  return gpu->makeResource<VulkanDrawable>(window->colorSpace());
}

VulkanDrawable::VulkanDrawable(std::shared_ptr<ColorSpace> colorSpace)
    : Drawable(0, 0, std::move(colorSpace)) {
  // The frame size is not known until the frame is imported; Drawable::import() backfills it
  // from the resolved render target.
}

VulkanDrawable::~VulkanDrawable() {
  // Discard the frame if it was never delivered; the manual-present proxy is released later in
  // onRelease(), inside the GPU's synchronized scope.
  abandon();
}

void VulkanDrawable::onRelease(VulkanGPU*) {
  // Releasing the imported target destroys the proxy (and its unused semaphores) here, inside
  // the device's synchronized scope.
  _importedTarget = nullptr;
}

std::shared_ptr<RenderTargetProxy> VulkanDrawable::onImport(Context* context) {
  if (_window == nullptr) {
    return nullptr;
  }
  auto window = static_cast<VulkanWindow*>(_window.get());
  return window->createSwapchainProxy(context, true);
}

void VulkanDrawable::onAttachSubmission(Context*) {
  // Wire the manual frame's acquire/present semaphore pair into the upcoming render submission,
  // regardless of whether the presentation has been registered: the render submission must wait
  // for the acquire semaphore, and manual presentations always defer the actual vkQueuePresentKHR
  // to onPresent() -> presentFrame().
  if (_importedTarget == nullptr) {
    return;
  }
  auto proxy = std::static_pointer_cast<VulkanSwapchainProxy>(_importedTarget);
  proxy->schedulePresent();
}

void VulkanDrawable::onPresent(Context*) {
  if (_importedTarget == nullptr) {
    return;
  }
  auto proxy = std::static_pointer_cast<VulkanSwapchainProxy>(_importedTarget);
  proxy->presentFrame();
}

void VulkanDrawable::onAbandon() {
  if (_importedTarget == nullptr) {
    return;
  }
  // The acquired image can no longer be presented; force a swapchain rebuild so the image and
  // its semaphores are reclaimed without any GPU calls from here.
  auto proxy = std::static_pointer_cast<VulkanSwapchainProxy>(_importedTarget);
  proxy->discardFrame();
}

}  // namespace tgfx
