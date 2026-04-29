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

#include <string>
#include <vector>
#include "gpu/vulkan/VulkanAPI.h"
#include "gpu/vulkan/VulkanResource.h"
#include "tgfx/gpu/ShaderModule.h"

namespace tgfx {

class VulkanGPU;

/**
 * Vulkan shader module implementation. Compiles GLSL to SPIR-V via the shared ShaderCompiler, then
 * creates a VkShaderModule from the SPIR-V binary.
 */
class VulkanShaderModule : public ShaderModule, public VulkanResource {
 public:
  static std::shared_ptr<VulkanShaderModule> Make(VulkanGPU* gpu,
                                                  const ShaderModuleDescriptor& descriptor);

  VkShaderModule vulkanShaderModule() const {
    return shaderModule;
  }

  ShaderStage stage() const {
    return _stage;
  }

  const std::vector<uint32_t>& spirvBinary() const {
    return _spirvBinary;
  }

  const std::string& glslCode() const {
    return _glslCode;
  }

 protected:
  void onRelease(VulkanGPU* gpu) override;

 private:
  VulkanShaderModule(VulkanGPU* gpu, const ShaderModuleDescriptor& descriptor);
  ~VulkanShaderModule() override = default;

  VkShaderModule shaderModule = VK_NULL_HANDLE;
  ShaderStage _stage = ShaderStage::Vertex;
  std::vector<uint32_t> _spirvBinary;
  std::string _glslCode;

  friend class VulkanGPU;
};

}  // namespace tgfx
