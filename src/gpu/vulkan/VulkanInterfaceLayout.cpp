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

#include "VulkanInterfaceLayout.h"
#include <array>

namespace tgfx {
namespace {

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

}  // namespace

bool VulkanInterfaceLayout::Create(VkDevice device, const SPIRVInterfaceLayout& layout,
                                   std::vector<VkDescriptorSetLayout>* setLayouts,
                                   std::string* error) {
  if (device == VK_NULL_HANDLE || setLayouts == nullptr) {
    return Fail(error, "the Vulkan device or descriptor set layouts are null");
  }
  setLayouts->clear();
  std::array<std::vector<VkDescriptorSetLayoutBinding>, 3> bindings = {};
  for (const auto& binding : layout.bindings()) {
    if (binding.set >= bindings.size()) {
      return Fail(error, "the descriptor set is outside the Vulkan convention");
    }
    VkDescriptorSetLayoutBinding vkBinding = {};
    vkBinding.binding = binding.binding;
    vkBinding.descriptorCount = 1;
    if (binding.type == SLType::Void) {
      vkBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      vkBinding.stageFlags = binding.stage == ShaderStage::Vertex ? VK_SHADER_STAGE_VERTEX_BIT
                                                                  : VK_SHADER_STAGE_FRAGMENT_BIT;
    } else {
      vkBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      vkBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[binding.set].push_back(vkBinding);
  }

  for (size_t set = 0; set < bindings.size(); ++set) {
    VkDescriptorSetLayoutCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = static_cast<uint32_t>(bindings[set].size());
    createInfo.pBindings = bindings[set].data();
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &setLayout) != VK_SUCCESS) {
      Destroy(device, setLayouts);
      return Fail(error, "vkCreateDescriptorSetLayout failed");
    }
    setLayouts->push_back(setLayout);
  }
  return true;
}

void VulkanInterfaceLayout::Destroy(VkDevice device,
                                    std::vector<VkDescriptorSetLayout>* setLayouts) {
  if (device == VK_NULL_HANDLE || setLayouts == nullptr) {
    return;
  }
  for (const auto setLayout : *setLayouts) {
    if (setLayout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    }
  }
  setLayouts->clear();
}

}  // namespace tgfx
