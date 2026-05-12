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

#include "VulkanBuffer.h"
#include "VulkanGPU.h"
#include "VulkanUtil.h"
#include "core/utils/Log.h"
#include "vk_mem_alloc.h"

namespace tgfx {

static VkBufferUsageFlags ToVkBufferUsage(uint32_t usage) {
  VkBufferUsageFlags flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  if (usage & GPUBufferUsage::VERTEX) {
    flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  }
  if (usage & GPUBufferUsage::INDEX) {
    flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  }
  if (usage & GPUBufferUsage::UNIFORM) {
    flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  }
  return flags;
}

std::shared_ptr<VulkanBuffer> VulkanBuffer::Make(VulkanGPU* gpu, size_t size, uint32_t usage) {
  if (!gpu || size == 0) {
    return nullptr;
  }

  VkBufferCreateInfo bufferInfo = {};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = ToVkBufferUsage(usage);
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocInfo = {};
  allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
  if (usage & GPUBufferUsage::READBACK) {
    allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
  } else {
    allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
  }

  VkBuffer vkBuffer = VK_NULL_HANDLE;
  VmaAllocation vmaAllocation = VK_NULL_HANDLE;
  VmaAllocationInfo vmaAllocInfo = {};
  auto result = vmaCreateBuffer(gpu->allocator(), &bufferInfo, &allocInfo, &vkBuffer,
                                &vmaAllocation, &vmaAllocInfo);
  if (result != VK_SUCCESS) {
    LOGE("VulkanBuffer::Make() vmaCreateBuffer failed: %s", VkResultToString(result));
    return nullptr;
  }

  return gpu->makeResource<VulkanBuffer>(gpu->allocator(), size, usage, vkBuffer, vmaAllocation,
                                         vmaAllocInfo.pMappedData);
}

VulkanBuffer::VulkanBuffer(VmaAllocator allocator, size_t size, uint32_t usage, VkBuffer buffer,
                           VmaAllocation allocation, void* mappedData)
    : GPUBuffer(size, usage), vmaAllocator(allocator), buffer(buffer), allocation(allocation),
      persistentlyMapped(mappedData) {
}

void VulkanBuffer::onRelease(VulkanGPU* gpu) {
  if (buffer != VK_NULL_HANDLE) {
    vmaDestroyBuffer(gpu->allocator(), buffer, allocation);
    buffer = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;
    persistentlyMapped = nullptr;
  }
}

void* VulkanBuffer::map(size_t offset, size_t size) {
  if (buffer == VK_NULL_HANDLE || mappedPointer != nullptr) {
    return nullptr;
  }
  if (size == 0) {
    LOGE("VulkanBuffer::map() size cannot be 0!");
    return nullptr;
  }
  if (size == GPU_BUFFER_WHOLE_SIZE) {
    size = _size - offset;
  }
  if (offset + size > _size) {
    LOGE("VulkanBuffer::map() range out of bounds!");
    return nullptr;
  }
  if (persistentlyMapped == nullptr) {
    LOGE("VulkanBuffer::map() buffer is not host-visible!");
    return nullptr;
  }
  mappedOffset = static_cast<VkDeviceSize>(offset);
  mappedSize = static_cast<VkDeviceSize>(size);
  // For readback buffers (GPU→CPU), invalidate the CPU cache to ensure we see the latest GPU writes.
  if (_usage & GPUBufferUsage::READBACK) {
    vmaInvalidateAllocation(vmaAllocator, allocation, mappedOffset, mappedSize);
  }
  mappedPointer = static_cast<uint8_t*>(persistentlyMapped) + offset;
  return mappedPointer;
}

void VulkanBuffer::unmap() {
  if (mappedPointer == nullptr) {
    return;
  }
  // Only flush for CPU-to-GPU buffers (vertex, index, uniform). Readback buffers (GPU-to-CPU)
  // do not need flushing since the CPU is the reader, not the writer.
  if (!(_usage & GPUBufferUsage::READBACK)) {
    vmaFlushAllocation(vmaAllocator, allocation, mappedOffset, mappedSize);
  }
  mappedPointer = nullptr;
  mappedOffset = 0;
  mappedSize = 0;
}

bool VulkanBuffer::isReady() const {
  return buffer != VK_NULL_HANDLE;
}

}  // namespace tgfx
