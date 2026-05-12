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

#include "VulkanSemaphore.h"
#include "VulkanGPU.h"
#include "VulkanUtil.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<VulkanSemaphore> VulkanSemaphore::Make(VulkanGPU* gpu) {
  if (!gpu) {
    return nullptr;
  }
  if (!gpu->extensions().timelineSemaphore) {
    return nullptr;
  }

  VkSemaphoreTypeCreateInfo timelineInfo = {};
  timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
  timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  timelineInfo.initialValue = 0;

  VkSemaphoreCreateInfo semaphoreInfo = {};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphoreInfo.pNext = &timelineInfo;

  VkSemaphore semaphore = VK_NULL_HANDLE;
  auto result = vkCreateSemaphore(gpu->device(), &semaphoreInfo, nullptr, &semaphore);
  if (result != VK_SUCCESS) {
    LOGE("VulkanSemaphore::Make() vkCreateSemaphore failed: %s", VkResultToString(result));
    return nullptr;
  }

  return gpu->makeResource<VulkanSemaphore>(semaphore, static_cast<uint64_t>(0));
}

std::shared_ptr<VulkanSemaphore> VulkanSemaphore::MakeFrom(VulkanGPU* gpu, VkSemaphore semaphore,
                                                           uint64_t value) {
  if (!gpu || semaphore == VK_NULL_HANDLE) {
    return nullptr;
  }
  return gpu->makeResource<VulkanSemaphore>(semaphore, value, true);
}

VulkanSemaphore::VulkanSemaphore(VkSemaphore semaphore, uint64_t value, bool adopted)
    : _semaphore(semaphore), _value(value), _adopted(adopted) {
}

BackendSemaphore VulkanSemaphore::getBackendSemaphore() const {
  if (_semaphore == VK_NULL_HANDLE) {
    return {};
  }
  VulkanSyncInfo vulkanInfo = {};
  vulkanInfo.semaphore = reinterpret_cast<uint64_t>(_semaphore);
  vulkanInfo.value = _value;
  return BackendSemaphore(vulkanInfo);
}

void VulkanSemaphore::onRelease(VulkanGPU* gpu) {
  if (_semaphore != VK_NULL_HANDLE && !_adopted) {
    vkDestroySemaphore(gpu->device(), _semaphore, nullptr);
    _semaphore = VK_NULL_HANDLE;
  }
}

}  // namespace tgfx
