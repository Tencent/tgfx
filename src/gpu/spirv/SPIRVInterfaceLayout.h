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

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "gpu/ir/ProgramIR.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {

/// One descriptor binding in the layout the SPIR-V modules declare.
struct SPIRVDescriptorBinding {
  std::string name = {};
  uint32_t set = 0;
  uint32_t binding = 0;
  SLType type = SLType::Void;
  ShaderStage stage = ShaderStage::Fragment;
};

/// The descriptor-set convention shared by SPIRVShaderBuilder and the Vulkan pipeline layout:
/// vertex UBOs live in set 0, fragment UBOs in set 1, and combined image samplers in set 2.
class SPIRVInterfaceLayout {
 public:
  static uint32_t UniformSet(ShaderStage stage);
  static uint32_t SamplerSet();

  bool Build(const InterfaceIR& interface, std::string* error = nullptr);
  bool Append(const SPIRVInterfaceLayout& other, std::string* error = nullptr);

  bool BuildBindingLayout(BindingLayout* layout, std::string* error = nullptr) const;

  const std::vector<SPIRVDescriptorBinding>& bindings() const {
    return descriptorBindings;
  }

 private:
  std::vector<SPIRVDescriptorBinding> descriptorBindings = {};
};

}  // namespace tgfx
