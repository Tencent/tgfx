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
 * Vulkan texture backed by a platform HardwareBuffer.
 *
 * On Android the buffer is an AHardwareBuffer imported through
 * VK_ANDROID_external_memory_android_hardware_buffer; on Windows it is an ID3D11Texture2D
 * imported through VK_KHR_external_memory_win32 with a shared NT handle. Both paths produce a
 * VkImage bound to dedicated device memory that aliases the source buffer, giving zero-copy
 * sampling.
 *
 * Windows integration contract (mirrors the D3D12 backend):
 *  - Source ID3D11Texture2D must be created with
 *    D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX. The Vulkan
 *    driver rejects legacy KMT handles.
 *  - After any producer-side write (including the initial pInitialData upload passed to
 *    CreateTexture2D), the producer MUST call ID3D11DeviceContext::Flush() before releasing
 *    the keyed mutex. Unlike the D3D12 shared-handle path, Vulkan bypasses the D3D scheduler
 *    when it imports the memory, so an unflushed producer surfaces as zeroed pixels on the
 *    Vulkan side.
 *  - The keyed mutex key is hard-coded to 0. Producer releases with key 0 to hand ownership
 *    to Vulkan; the render path acquires/releases with key 0 around every vkQueueSubmit that
 *    samples the texture, so the producer can AcquireSync(0) again to write a new frame.
 *  - Only TextureUsage::TEXTURE_BINDING is supported: RENDER_ATTACHMENT would require the
 *    caller to also request BIND_RENDER_TARGET on the D3D11 side plus application-level
 *    fencing, none of which are part of the v1 contract.
 *
 * A VkWin32KeyedMutexAcquireReleaseInfoKHR pNext is attached to every vkQueueSubmit that
 * references imported memory, so the driver takes care of the actual acquire/release ordering
 * relative to the GPU commands.
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
  // Non-owning reference to the IDXGIKeyedMutex living on the source ID3D11Texture2D. We retain
  // it through the D3D11 texture (HardwareBufferRetain above), so this ComPtr just provides
  // typed access without additional lifetime concerns. Set only on the Win32 import path.
  Microsoft::WRL::ComPtr<IDXGIKeyedMutex> _keyedMutex = nullptr;
#endif

  friend class VulkanGPU;
};

}  // namespace tgfx

#endif  // defined(__ANDROID__) || defined(_WIN32)
