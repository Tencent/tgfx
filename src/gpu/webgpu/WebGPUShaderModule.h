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

#include <webgpu/webgpu_cpp.h>
#include "tgfx/gpu/ShaderModule.h"

namespace tgfx {
class WebGPUShaderModule : public ShaderModule {
 public:
  explicit WebGPUShaderModule(wgpu::ShaderModule shaderModule)
      : _shaderModule(std::move(shaderModule)) {
  }

  wgpu::ShaderModule wgpuShaderModule() const {
    return _shaderModule;
  }

 private:
  wgpu::ShaderModule _shaderModule = nullptr;
};
}  // namespace tgfx
