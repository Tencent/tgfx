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
#include "tgfx/gpu/Device.h"

namespace tgfx {

/**
 * VulkanDevice manages a VkInstance, VkPhysicalDevice, VkDevice, and VkQueue. It serves as the
 * primary entry point for creating a Vulkan rendering context.
 */
class VulkanDevice : public Device {
 public:
  /**
   * Creates a new VulkanDevice with default settings. Returns nullptr if Vulkan is not available on
   * the system (e.g., no Vulkan driver installed or required extensions are not supported), allowing
   * the caller to gracefully fall back to another GPU backend.
   */
  static std::shared_ptr<VulkanDevice> Make();

 protected:
  VulkanDevice();
};

}  // namespace tgfx
