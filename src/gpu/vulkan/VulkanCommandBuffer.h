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
#include <vector>
#include "gpu/vulkan/VulkanFrameSession.h"
#include "tgfx/gpu/CommandBuffer.h"

namespace tgfx {

/**
 * Transport container that carries a FrameSession from encoding to submission.
 *
 * Created by VulkanCommandEncoder::onFinish() which moves its FrameSession here. Consumed by
 * VulkanCommandQueue::submit() which moves the session into VulkanGPU's InflightSubmission.
 * After submit(), this object is empty and may be discarded. Destructor does NOT call any Vulkan
 * API — if the CommandBuffer is abandoned (never submitted), the FrameSession's vectors of
 * shared_ptr will decrement refcounts naturally, and deferred Vulkan objects will leak (debug
 * assert can catch this misuse).
 */
class VulkanCommandBuffer : public CommandBuffer {
 public:
  explicit VulkanCommandBuffer(FrameSession session) : session(std::move(session)) {
  }
  ~VulkanCommandBuffer() override = default;

  FrameSession& frameSession() {
    return session;
  }

  VkCommandBuffer vulkanCommandBuffer() const {
    return session.commandBuffer;
  }

  VkCommandPool vulkanCommandPool() const {
    return session.commandPool;
  }

 private:
  FrameSession session;
};

}  // namespace tgfx
