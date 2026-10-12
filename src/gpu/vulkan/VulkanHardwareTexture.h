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

#if defined(__ANDROID__) || defined(_WIN32)

#pragma once

#include "gpu/vulkan/VulkanTexture.h"
#include "tgfx/platform/HardwareBuffer.h"

#if defined(_WIN32) && !defined(__ANDROID__)
#include <wrl/client.h>
struct IDXGIKeyedMutex;
#endif

namespace tgfx {

class VulkanGPU;

/**
 * Vulkan texture backed by a platform HardwareBuffer, imported via
 * VK_ANDROID_external_memory_android_hardware_buffer on Android or via
 * VK_KHR_external_memory_win32 + shared NT handle on Windows. Produces a VkImage aliased over the
 * source buffer's memory, giving zero-copy sampling. Only TextureUsage::TEXTURE_BINDING is
 * supported. On Windows the source ID3D11Texture2D must have been created with
 * D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX, and the producer
 * must Flush() before ReleaseSync(0) so the imported memory sees committed pixels; the render
 * path chains VkWin32KeyedMutexAcquireReleaseInfoKHR (key = 0) around every vkQueueSubmit.
 */
class VulkanHardwareTexture : public VulkanTexture {
 public:
  static std::shared_ptr<VulkanHardwareTexture> MakeFrom(VulkanGPU* gpu,
                                                         HardwareBufferRef hardwareBuffer,
                                                         uint32_t usage);
  ~VulkanHardwareTexture() override;

  HardwareBufferRef getHardwareBuffer() const override {
    return hardwareBuffer;
  }

  VkDeviceMemory importedMemoryForKeyedMutex() const override {
#if defined(_WIN32) && !defined(__ANDROID__)
    return _keyedMutex ? dedicatedMemory : VK_NULL_HANDLE;
#else
    return VK_NULL_HANDLE;
#endif
  }

 protected:
  void onRelease(VulkanGPU* gpu) override;

 private:
  VulkanHardwareTexture(const TextureDescriptor& descriptor, HardwareBufferRef hardwareBuffer,
                        VkImage image, VkImageView imageView, VkDeviceMemory dedicatedMemory,
                        VkSamplerYcbcrConversion ycbcrConversion, VkFormat format,
                        VkImageLayout initialLayout);

  HardwareBufferRef hardwareBuffer = nullptr;
  VkDeviceMemory dedicatedMemory = VK_NULL_HANDLE;
  VkSamplerYcbcrConversion ycbcrConversion = VK_NULL_HANDLE;

#if defined(_WIN32) && !defined(__ANDROID__)
  // Owning ComPtr to the IDXGIKeyedMutex queried off the source ID3D11Texture2D. QueryInterface
  // returns a strong reference, so this ComPtr independently keeps the mutex object alive.
  Microsoft::WRL::ComPtr<IDXGIKeyedMutex> _keyedMutex = nullptr;
#endif

  friend class VulkanGPU;
};

}  // namespace tgfx

#endif  // defined(__ANDROID__) || defined(_WIN32)
