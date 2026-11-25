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
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "SPIRVInterfaceLayout.h"
#include <unordered_set>
#include "gpu/ShaderBindingLayout.h"

namespace tgfx {

uint32_t SPIRVInterfaceLayout::UniformSet(ShaderStage stage) {
  return stage == ShaderStage::Vertex ? static_cast<uint32_t>(VERTEX_UBO_DESCRIPTOR_SET)
                                      : static_cast<uint32_t>(FRAGMENT_UBO_DESCRIPTOR_SET);
}

uint32_t SPIRVInterfaceLayout::SamplerSet() {
  return static_cast<uint32_t>(TEXTURE_DESCRIPTOR_SET);
}

bool SPIRVInterfaceLayout::Build(const InterfaceIR& interface, std::string* error) {
  descriptorBindings.clear();
  for (const auto& block : interface.uniformBlocks) {
    SPIRVDescriptorBinding binding = {};
    binding.name = block.name;
    binding.set = UniformSet(block.stage);
    binding.binding = block.binding;
    binding.stage = block.stage;
    if (!AddBinding(binding, error)) {
      return false;
    }
  }
  for (const auto& sampler : interface.samplers) {
    SPIRVDescriptorBinding binding = {};
    binding.name = sampler.name;
    binding.set = SamplerSet();
    binding.binding = sampler.binding;
    binding.type = sampler.type;
    binding.stage = ShaderStage::Fragment;
    if (!AddBinding(binding, error)) {
      return false;
    }
  }
  return true;
}

bool SPIRVInterfaceLayout::AddBinding(const SPIRVDescriptorBinding& binding, std::string* error) {
  for (const auto& existing : descriptorBindings) {
    if (existing.set == binding.set && existing.binding == binding.binding) {
      if (error != nullptr) {
        *error = "descriptor set " + std::to_string(binding.set) + " binding " +
                 std::to_string(binding.binding) + " is declared twice";
      }
      return false;
    }
  }
  descriptorBindings.push_back(binding);
  return true;
}

bool SPIRVInterfaceLayout::BuildBindingLayout(BindingLayout* layout, std::string* error) const {
  if (layout == nullptr) {
    if (error != nullptr) {
      *error = "the binding layout is null";
    }
    return false;
  }
  layout->uniformBlocks.clear();
  layout->textureSamplers.clear();
  unsigned logicalBinding = 0;
  for (const auto& binding : descriptorBindings) {
    const uint32_t visibility = binding.stage == ShaderStage::Vertex ? ShaderVisibility::Vertex
                                                                     : ShaderVisibility::Fragment;
    if (binding.type == SLType::Void) {
      layout->uniformBlocks.emplace_back(binding.name, logicalBinding++, visibility);
    } else {
      layout->textureSamplers.emplace_back(binding.name, logicalBinding++,
                                           ShaderVisibility::Fragment);
    }
  }
  return true;
}

bool SPIRVInterfaceLayout::Append(const SPIRVInterfaceLayout& other, std::string* error) {
  for (const auto& binding : other.descriptorBindings) {
    for (const auto& existing : descriptorBindings) {
      if (existing.set == binding.set && existing.binding == binding.binding) {
        if (error != nullptr) {
          *error = "descriptor set " + std::to_string(binding.set) + " binding " +
                   std::to_string(binding.binding) + " is declared twice";
        }
        return false;
      }
    }
    descriptorBindings.push_back(binding);
  }
  return true;
}

}  // namespace tgfx
