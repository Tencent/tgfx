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

#include "gpu/vulkan/VulkanAPI.h"
#include "tgfx/gpu/CommandBuffer.h"

namespace tgfx {

/**
 * Vulkan command buffer implementation.
 */
class VulkanCommandBuffer : public CommandBuffer {
 public:
  explicit VulkanCommandBuffer(VkCommandBuffer commandBuffer, VkCommandPool commandPool);
  ~VulkanCommandBuffer() override = default;

  VkCommandBuffer vulkanCommandBuffer() const {
    return commandBuffer;
  }

  VkCommandPool vulkanCommandPool() const {
    return commandPool;
  }

 private:
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkCommandPool commandPool = VK_NULL_HANDLE;
};

}  // namespace tgfx
