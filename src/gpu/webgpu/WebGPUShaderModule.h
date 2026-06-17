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

#include <webgpu/webgpu.h>
#include <string>
#include "WebGPUResource.h"
#include "tgfx/gpu/ShaderModule.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {

class WebGPUGPU;

class WebGPUShaderModule : public ShaderModule, public WebGPUResource {
 public:
  static std::shared_ptr<WebGPUShaderModule> Make(WebGPUGPU* gpu,
                                                  const ShaderModuleDescriptor& descriptor);

  WGPUShaderModule webgpuShaderModule() const {
    return shaderModule;
  }

  ShaderStage stage() const {
    return _stage;
  }

  void onRelease(WebGPUGPU* gpu) override;

 private:
  WebGPUShaderModule(WebGPUGPU* gpu, const ShaderModuleDescriptor& descriptor);

  bool compileShader(WGPUDevice device, const std::string& glslCode, ShaderStage stage);

  WGPUShaderModule shaderModule = nullptr;
  ShaderStage _stage = ShaderStage::Vertex;

  friend class WebGPUGPU;
};

}  // namespace tgfx
