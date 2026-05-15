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
#include "tgfx/gpu/GPUBuffer.h"

VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

namespace tgfx {

class VulkanGPU;

/**
 * Vulkan buffer implementation.
 */
class VulkanBuffer : public GPUBuffer, public VulkanResource {
 public:
  static std::shared_ptr<VulkanBuffer> Make(VulkanGPU* gpu, size_t size, uint32_t usage);

  VkBuffer vulkanBuffer() const {
    return buffer;
  }

  void* map(size_t offset, size_t size) override;
  void unmap() override;
  bool isReady() const override;

 protected:
  void onRelease(VulkanGPU* gpu) override;

 private:
  VulkanBuffer(VmaAllocator allocator, size_t size, uint32_t usage, VkBuffer buffer,
               VmaAllocation allocation, void* mappedData);
  ~VulkanBuffer() override = default;

  VmaAllocator vmaAllocator = VK_NULL_HANDLE;
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  void* persistentlyMapped = nullptr;
  void* mappedPointer = nullptr;
  VkDeviceSize mappedOffset = 0;
  VkDeviceSize mappedSize = 0;

  friend class VulkanGPU;
};

}  // namespace tgfx
