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

#include <cstddef>
#include <memory>
#include "gpu/vulkan/VulkanAPI.h"

namespace tgfx {

inline bool VulkanSupportsReadback(VkImageUsageFlags supportedUsage) {
  return (supportedUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
}

class VulkanManualToken {
 public:
  bool acquire() {
    if (active) {
      return false;
    }
    active = true;
    return true;
  }

  void release() {
    active = false;
  }

  bool isActive() const {
    return active;
  }

 private:
  bool active = false;
};

struct VulkanSubmissionPlan {
  size_t acquireWaitCount = 0;
  size_t signalCount = 0;
  size_t barrierCount = 0;
  size_t presentCount = 0;
  size_t ownedSemaphoreCount = 0;

  void add(bool manualPresent) {
    acquireWaitCount++;
    ownedSemaphoreCount++;
    if (!manualPresent) {
      signalCount++;
      barrierCount++;
      presentCount++;
    }
  }
};

enum class VulkanManualPhase { Acquired, RenderSubmitted, Presented };

class VulkanFrameState {
 public:
  explicit VulkanFrameState(bool manual) : manual(manual) {
  }

  bool isManual() const {
    return manual;
  }

  VulkanManualPhase phase() const {
    return currentPhase;
  }

  void markRenderSubmitted() {
    if (currentPhase == VulkanManualPhase::Acquired) {
      currentPhase = VulkanManualPhase::RenderSubmitted;
    }
  }

  bool beginPresent() {
    if (!manual || currentPhase != VulkanManualPhase::RenderSubmitted) {
      return false;
    }
    currentPhase = VulkanManualPhase::Presented;
    return true;
  }

 private:
  bool manual = false;
  VulkanManualPhase currentPhase = VulkanManualPhase::Acquired;
};

struct VulkanSwapchainImageState {
  std::shared_ptr<VkImageLayout> layout =
      std::make_shared<VkImageLayout>(VK_IMAGE_LAYOUT_UNDEFINED);
  VkSemaphore presentSemaphore = VK_NULL_HANDLE;
};

}  // namespace tgfx
