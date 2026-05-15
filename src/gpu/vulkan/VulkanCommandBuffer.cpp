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

#include "VulkanCommandBuffer.h"
#include "VulkanGPU.h"

namespace tgfx {

VulkanCommandBuffer::~VulkanCommandBuffer() {
  if (session.commandPool == VK_NULL_HANDLE) {
    // Session was already moved out by submit(). Normal path — nothing to clean up.
    return;
  }
  // Abandon path: CommandBuffer was created (finish() succeeded) but never submitted.
  // Reclaim all session resources through the same unified path used by reclaimSubmission().
  _gpu->reclaimAbandonedSession(std::move(session));
}

}  // namespace tgfx
