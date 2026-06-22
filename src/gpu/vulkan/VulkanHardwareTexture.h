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

#if defined(__ANDROID__)

#pragma once

#include "gpu/vulkan/VulkanAPI.h"
#include "gpu/vulkan/VulkanResource.h"
#include "tgfx/gpu/Texture.h"
#include "tgfx/platform/HardwareBuffer.h"

namespace tgfx {

class VulkanGPU;

/**
 * Vulkan texture backed by an Android AHardwareBuffer. Imports the hardware buffer as external
 * memory and creates a VkImage bound to it for zero-copy texture sharing.
 */
class VulkanHardwareTexture : public Texture, public VulkanResource {
 public:
  static std::shared_ptr<VulkanHardwareTexture> MakeFrom(VulkanGPU* gpu,
                                                         HardwareBufferRef hardwareBuffer,
                                                         uint32_t usage);

  VkImage vulkanImage() const {
    return image;
  }

  VkImageView vulkanImageView() const {
    return imageView;
  }

  VkImageView vulkanRenderImageView() const {
    return imageView;
  }

  VkFormat vulkanFormat() const {
    return format;
  }

  VkImageLayout currentLayout() const {
    return layout;
  }

  void setCurrentLayout(VkImageLayout newLayout) {
    layout = newLayout;
  }

  HardwareBufferRef getHardwareBuffer() const override {
    return hardwareBuffer;
  }

  BackendTexture getBackendTexture() const override;
  BackendRenderTarget getBackendRenderTarget() const override;

 protected:
  void onRelease(VulkanGPU* gpu) override;

 private:
  VulkanHardwareTexture(const TextureDescriptor& descriptor, HardwareBufferRef hardwareBuffer,
                        VkImage image, VkImageView imageView, VkDeviceMemory dedicatedMemory,
                        VkSamplerYcbcrConversion ycbcrConversion, VkFormat format);
  ~VulkanHardwareTexture() override;

  HardwareBufferRef hardwareBuffer = nullptr;
  VkImage image = VK_NULL_HANDLE;
  VkImageView imageView = VK_NULL_HANDLE;
  VkDeviceMemory dedicatedMemory = VK_NULL_HANDLE;
  VkSamplerYcbcrConversion ycbcrConversion = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

  friend class VulkanGPU;
};

}  // namespace tgfx

#endif
