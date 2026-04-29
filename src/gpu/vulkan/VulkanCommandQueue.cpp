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

#include "VulkanCommandQueue.h"
#include "VulkanGPU.h"
#include "core/utils/Log.h"

namespace tgfx {

VulkanCommandQueue::VulkanCommandQueue(VulkanGPU* gpu) : gpu(gpu) {
}

VulkanCommandQueue::~VulkanCommandQueue() {
}

void VulkanCommandQueue::writeBuffer(std::shared_ptr<GPUBuffer>, size_t, const void*, size_t) {
  // TODO: Implement staging buffer upload.
}

void VulkanCommandQueue::writeTexture(std::shared_ptr<Texture>, const Rect&, const void*,
                                      size_t) {
  // TODO: Implement staging buffer → image copy.
}

void VulkanCommandQueue::submit(std::shared_ptr<CommandBuffer>) {
  // TODO: Implement vkQueueSubmit.
}

std::shared_ptr<Semaphore> VulkanCommandQueue::insertSemaphore() {
  // TODO: Implement timeline semaphore insertion.
  return nullptr;
}

void VulkanCommandQueue::waitSemaphore(std::shared_ptr<Semaphore>) {
  // TODO: Implement GPU wait on semaphore.
}

void VulkanCommandQueue::waitUntilCompleted() {
  if (gpu == nullptr) {
    return;
  }
  auto device = gpu->device();
  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
  }
}

}  // namespace tgfx
