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

#include <unordered_map>
#include <vector>
#include "core/utils/EnumHasher.h"
#include "gpu/vulkan/VulkanAPI.h"
#include "gpu/vulkan/VulkanExtensions.h"
#include "tgfx/gpu/GPUFeatures.h"
#include "tgfx/gpu/GPUInfo.h"
#include "tgfx/gpu/GPULimits.h"
#include "tgfx/gpu/PixelFormat.h"

namespace tgfx {

/**
 * Vulkan GPU capabilities and limits. Extension-related queries are handled by VulkanExtensions;
 * this class focuses on format support, sample counts, and hardware limits.
 */
class VulkanCaps {
 public:
  VulkanCaps(VkPhysicalDevice physicalDevice, const VulkanExtensions& extensions);

  const GPUInfo* info() const {
    return &_info;
  }

  const GPUFeatures* features() const {
    return &_features;
  }

  const GPULimits* limits() const {
    return &_limits;
  }

  bool isFormatRenderable(PixelFormat format) const;

  bool isFormatAsColorAttachment(PixelFormat format) const;

  int getSampleCount(int requestedCount, PixelFormat format) const;

  VkFormat getVkFormat(PixelFormat format) const;

  bool multisampleDisableSupport() const {
    return true;
  }

  bool frameBufferFetchSupport() const {
    return frameBufferFetchSupported;
  }

  bool textureRedSupport() const {
    return true;
  }

  bool clampToBorderSupport() const {
    return true;
  }

  bool npotTextureTileModeSupport() const {
    return true;
  }

  bool mipMapSupport() const {
    return true;
  }

  bool bufferMapSupport() const {
    return true;
  }

  bool computeSupport() const {
    return true;
  }

 private:
  struct FormatInfo {
    VkFormat vulkanFormat = VK_FORMAT_UNDEFINED;
    bool renderable = false;
    bool colorAttachment = false;
    std::vector<int> sampleCounts = {};
  };

  void initFeatures(VkPhysicalDevice physicalDevice, const VulkanExtensions& extensions);
  void initLimits(const VkPhysicalDeviceProperties& properties);
  void initFormatTable(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceProperties& properties);
  FormatInfo checkFormat(VkPhysicalDevice physicalDevice, VkFormat vulkanFormat,
                         const VkPhysicalDeviceProperties& properties) const;

  GPUInfo _info = {};
  GPUFeatures _features = {};
  GPULimits _limits = {};

  bool frameBufferFetchSupported = false;
  std::unordered_map<PixelFormat, FormatInfo, EnumHasher> formatTable = {};
};

}  // namespace tgfx
