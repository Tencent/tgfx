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

#include "VulkanShaderModule.h"
#include "VulkanGPU.h"
#include "core/utils/Log.h"
#include "gpu/ShaderCompiler.h"

namespace tgfx {

std::shared_ptr<VulkanShaderModule> VulkanShaderModule::Make(
    VulkanGPU* gpu, const ShaderModuleDescriptor& descriptor) {
  if (!gpu) {
    return nullptr;
  }
  auto module = gpu->makeResource<VulkanShaderModule>(gpu, descriptor);
  if (module->vulkanShaderModule() == VK_NULL_HANDLE) {
    return nullptr;
  }
  return module;
}

VulkanShaderModule::VulkanShaderModule(VulkanGPU* gpu, const ShaderModuleDescriptor& descriptor) {
  std::string vulkanGLSL = PreprocessGLSL(descriptor.code);
  auto spirvBinary = CompileGLSLToSPIRV(gpu->shaderCompiler(), vulkanGLSL, descriptor.stage);
  if (spirvBinary.empty()) {
    LOGE("VulkanShaderModule: GLSL to SPIR-V compilation failed.");
    return;
  }

  VkShaderModuleCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = spirvBinary.size() * sizeof(uint32_t);
  createInfo.pCode = spirvBinary.data();

  auto result = vkCreateShaderModule(gpu->device(), &createInfo, nullptr, &shaderModule);
  if (result != VK_SUCCESS) {
    LOGE("VulkanShaderModule: vkCreateShaderModule failed.");
    shaderModule = VK_NULL_HANDLE;
  }
}

void VulkanShaderModule::onRelease(VulkanGPU* gpu) {
  if (shaderModule != VK_NULL_HANDLE) {
    vkDestroyShaderModule(gpu->device(), shaderModule, nullptr);
    shaderModule = VK_NULL_HANDLE;
  }
}

}  // namespace tgfx
