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

#include "utils/NativeShader.h"
#include <string>

namespace tgfx {

bool FillNativeShaderDescriptor(GPU* gpu, ShaderStage stage, const NativeShaderStage& source,
                                ShaderModuleDescriptor* desc) {
  if (gpu == nullptr || desc == nullptr) {
    return false;
  }
  desc->stage = stage;
  desc->uniformSlots = source.uniformSlots;
  switch (gpu->info()->backend) {
    case Backend::OpenGL: {
      if (source.glsl == nullptr) {
        return false;
      }
      auto isDesktop = gpu->info()->version.find("OpenGL ES") == std::string::npos;
      desc->format = ShaderCodeFormat::GLSL;
      desc->code = isDesktop ? "#version 150\n\n" + std::string(source.glsl)
                             : "#version 300 es\n\n" + std::string(source.glsl);
      return true;
    }
    case Backend::Metal:
      if (source.msl == nullptr) {
        return false;
      }
      desc->format = ShaderCodeFormat::MSL;
      desc->code = source.msl;
      return true;
    case Backend::Vulkan:
      if (source.spirv == nullptr || source.spirvLength == 0) {
        return false;
      }
      desc->format = ShaderCodeFormat::SPIRV;
      desc->binaryData.assign(
          reinterpret_cast<const uint8_t*>(source.spirv),
          reinterpret_cast<const uint8_t*>(source.spirv) + source.spirvLength * sizeof(uint32_t));
      return true;
    case Backend::WebGPU:
      if (source.wgsl == nullptr) {
        return false;
      }
      desc->format = ShaderCodeFormat::WGSL;
      desc->code = source.wgsl;
      return true;
    case Backend::D3D12:
      if (source.hlsl == nullptr) {
        return false;
      }
      desc->format = ShaderCodeFormat::HLSL;
      desc->code = source.hlsl;
      return true;
    default:
      return false;
  }
}

bool CreateNativeShaderModules(GPU* gpu, const NativeShaderStage& vertexSource,
                               const NativeShaderStage& fragmentSource,
                               std::shared_ptr<ShaderModule>* vertexModule,
                               std::shared_ptr<ShaderModule>* fragmentModule) {
  ShaderModuleDescriptor vertexDesc = {};
  if (!FillNativeShaderDescriptor(gpu, ShaderStage::Vertex, vertexSource, &vertexDesc)) {
    return false;
  }
  auto vertex = gpu->createShaderModule(vertexDesc);
  if (vertex == nullptr) {
    return false;
  }
  ShaderModuleDescriptor fragmentDesc = {};
  if (!FillNativeShaderDescriptor(gpu, ShaderStage::Fragment, fragmentSource, &fragmentDesc)) {
    return false;
  }
  auto fragment = gpu->createShaderModule(fragmentDesc);
  if (fragment == nullptr) {
    return false;
  }
  *vertexModule = std::move(vertex);
  *fragmentModule = std::move(fragment);
  return true;
}

}  // namespace tgfx
