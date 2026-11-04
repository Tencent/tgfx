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

#include "WebGPUShaderModule.h"
#include "WebGPUGPU.h"
#include "core/utils/Log.h"
#include "gpu/ShaderInterface.h"

namespace tgfx {

std::shared_ptr<WebGPUShaderModule> WebGPUShaderModule::Make(
    WebGPUGPU* gpu, const ShaderModuleDescriptor& descriptor) {
  if (gpu == nullptr) {
    return nullptr;
  }
  return gpu->makeResource<WebGPUShaderModule>(gpu, descriptor);
}

WebGPUShaderModule::WebGPUShaderModule(WebGPUGPU* gpu, const ShaderModuleDescriptor& descriptor)
    : VaryingShaderModule(ExtractModuleVaryingDecls(descriptor), {}), _stage(descriptor.stage) {
  if (descriptor.format != ShaderCodeFormat::WGSL) {
    LOGE(
        "WebGPUShaderModule: unsupported shader format; the WebGPU backend only accepts WGSL "
        "text.");
    return;
  }
  if (descriptor.code.empty()) {
    LOGE("WebGPUShaderModule: WGSL source is empty");
    return;
  }
  // Precompiled WGSL text path: the artifacts carry no parseable GLSL, so the baked block
  // bindings arrive via the descriptor.
  setUniformSlots(descriptor.uniformSlots);
  WGPUShaderModuleWGSLDescriptor wgslDescriptor = {};
  wgslDescriptor.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
  wgslDescriptor.code = descriptor.code.c_str();
  WGPUShaderModuleDescriptor moduleDesc = {};
  moduleDesc.nextInChain = &wgslDescriptor.chain;
  shaderModule = wgpuDeviceCreateShaderModule(gpu->device(), &moduleDesc);
  if (shaderModule == nullptr) {
    LOGE("WebGPUShaderModule: WGSL module creation failed");
  }
}

void WebGPUShaderModule::onRelease(WebGPUGPU*) {
  if (shaderModule != nullptr) {
    wgpuShaderModuleRelease(shaderModule);
    shaderModule = nullptr;
  }
}

}  // namespace tgfx
