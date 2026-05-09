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

class VulkanGPU;

/**
 * Transport container that carries a FrameSession from encoding to submission.
 *
 * Created by VulkanCommandEncoder::onFinish() which moves its FrameSession here. Consumed by
 * VulkanCommandQueue::submit() which moves the session into VulkanGPU's InflightSubmission.
 * After submit(), this object is empty and may be discarded.
 *
 * If the CommandBuffer is abandoned (created but never submitted), the destructor reclaims all
 * session resources via VulkanGPU::reclaimAbandonedSession(). This matches the abandon safety
 * guarantee provided by VulkanCommandEncoder::onRelease() — both use the same unified cleanup
 * path in VulkanGPU, ensuring no Vulkan objects are leaked regardless of where the pipeline is
 * interrupted.
 */
class VulkanCommandBuffer : public CommandBuffer {
 public:
  VulkanCommandBuffer(VulkanGPU* gpu, FrameSession session)
      : _gpu(gpu), session(std::move(session)) {
  }

  ~VulkanCommandBuffer() override;

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
  VulkanGPU* _gpu = nullptr;
  FrameSession session;
};

}  // namespace tgfx
