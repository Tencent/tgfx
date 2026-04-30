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

#include "VulkanSampler.h"
#include "VulkanGPU.h"
#include "VulkanUtil.h"

namespace tgfx {

static VkFilter ToVkFilter(FilterMode mode) {
  switch (mode) {
    case FilterMode::Nearest:
      return VK_FILTER_NEAREST;
    case FilterMode::Linear:
      return VK_FILTER_LINEAR;
    default:
      return VK_FILTER_NEAREST;
  }
}

static VkSamplerMipmapMode ToVkMipmapMode(MipmapMode mode) {
  switch (mode) {
    case MipmapMode::Nearest:
      return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case MipmapMode::Linear:
      return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    default:
      return VK_SAMPLER_MIPMAP_MODE_NEAREST;
  }
}

static VkSamplerAddressMode ToVkAddressMode(AddressMode mode) {
  switch (mode) {
    case AddressMode::ClampToEdge:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case AddressMode::Repeat:
      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case AddressMode::MirrorRepeat:
      return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case AddressMode::ClampToBorder:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    default:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
}

std::shared_ptr<VulkanSampler> VulkanSampler::Make(VulkanGPU* gpu,
                                                   const SamplerDescriptor& descriptor) {
  if (!gpu) {
    return nullptr;
  }

  VkSamplerCreateInfo samplerInfo = {};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.minFilter = ToVkFilter(descriptor.minFilter);
  samplerInfo.magFilter = ToVkFilter(descriptor.magFilter);
  samplerInfo.mipmapMode = ToVkMipmapMode(descriptor.mipmapMode);
  samplerInfo.addressModeU = ToVkAddressMode(descriptor.addressModeX);
  samplerInfo.addressModeV = ToVkAddressMode(descriptor.addressModeY);
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1.0f;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.minLod = 0.0f;
  // When mipmap is disabled, clamp maxLod to 0 to prevent sampling uninitialized mip levels.
  // Textures may be created with multiple mip levels but only have level 0 data uploaded.
  samplerInfo.maxLod = (descriptor.mipmapMode == MipmapMode::None) ? 0.0f : VK_LOD_CLAMP_NONE;
  samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;

  VkSampler vkSampler = VK_NULL_HANDLE;
  auto result = vkCreateSampler(gpu->device(), &samplerInfo, nullptr, &vkSampler);
  if (result != VK_SUCCESS) {
    return nullptr;
  }

  return gpu->makeResource<VulkanSampler>(vkSampler);
}

VulkanSampler::VulkanSampler(VkSampler sampler) : sampler(sampler) {
}

void VulkanSampler::onRelease(VulkanGPU* gpu) {
  if (sampler != VK_NULL_HANDLE) {
    vkDestroySampler(gpu->device(), sampler, nullptr);
    sampler = VK_NULL_HANDLE;
  }
}

}  // namespace tgfx
