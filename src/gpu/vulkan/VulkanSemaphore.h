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
#include "gpu/vulkan/VulkanResource.h"
#include "tgfx/gpu/Semaphore.h"

namespace tgfx {

class VulkanGPU;

/**
 * Vulkan semaphore implementation using VK_KHR_timeline_semaphore for GPU synchronization.
 */
class VulkanSemaphore : public Semaphore, public VulkanResource {
 public:
  static std::shared_ptr<VulkanSemaphore> Make(VulkanGPU* gpu);

  static std::shared_ptr<VulkanSemaphore> MakeFrom(VulkanGPU* gpu, VkSemaphore semaphore,
                                                   uint64_t value);

  VulkanSemaphore(VkSemaphore semaphore, uint64_t value);
  ~VulkanSemaphore() override = default;

  VkSemaphore vulkanSemaphore() const {
    return _semaphore;
  }

  uint64_t signalValue() const {
    return _value;
  }

  uint64_t nextSignalValue() {
    return ++_value;
  }

  BackendSemaphore getBackendSemaphore() const override;

 protected:
  void onRelease(VulkanGPU* gpu) override;

 private:
  VkSemaphore _semaphore = VK_NULL_HANDLE;
  uint64_t _value = 0;

  friend class VulkanGPU;
};

}  // namespace tgfx
