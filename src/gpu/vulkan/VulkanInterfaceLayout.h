/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>
#include "VulkanAPI.h"
#include "gpu/spirv/SPIRVInterfaceLayout.h"

namespace tgfx {

/// Turns the SPIR-V descriptor convention into the three Vulkan descriptor set layouts:
/// set 0 for vertex UBOs, set 1 for fragment UBOs and set 2 for combined image samplers.
class VulkanInterfaceLayout {
 public:
  static bool Create(VkDevice device, const SPIRVInterfaceLayout& layout,
                     std::vector<VkDescriptorSetLayout>* setLayouts, std::string* error = nullptr);

  static void Destroy(VkDevice device, std::vector<VkDescriptorSetLayout>* setLayouts);
};

}  // namespace tgfx
