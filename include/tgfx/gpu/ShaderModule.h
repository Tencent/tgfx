/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include <cstdint>
#include <string>
#include <vector>
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {

/**
 * Describes the format of shader code stored in a ShaderModuleDescriptor.
 */
enum class ShaderCodeFormat {
  /**
   * GLSL text source code. This is the default format used by ProgramBuilder.
   */
  GLSL,
  /**
   * Pre-compiled SPIR-V binary. Used by the Vulkan backend to skip runtime GLSL-to-SPIRV
   * compilation.
   */
  SPIRV,
  /**
   * Metal Shading Language text source code. Used by the Metal backend when loading precompiled
   * shader source.
   */
  MSL,
  /**
   * WebGPU Shading Language text source code. Used by the WebGPU backend when loading precompiled
   * shader source.
   */
  WGSL,
};

/**
 * ShaderModuleDescriptor describes the properties required to create a ShaderModule.
 */
class ShaderModuleDescriptor {
 public:
  /**
   * The shader text source code (GLSL, MSL, or WGSL). When format is SPIRV, this field is ignored
   * and binaryData is used instead.
   */
  std::string code;

  /**
   * Pre-compiled shader binary data (e.g. SPIR-V). Only used when format is SPIRV.
   */
  std::vector<uint8_t> binaryData;

  /**
   * The format of the shader code or binary. Defaults to GLSL for backward compatibility with the
   * existing ProgramBuilder path.
   */
  ShaderCodeFormat format = ShaderCodeFormat::GLSL;

  /**
   * Specifies the shader stage (e.g., vertex, fragment, compute). Only relevant for the OpenGL
   * backend; ignored by other backends.
   */
  ShaderStage stage = ShaderStage::Vertex;
};

/**
 * ShaderModule is an internal object that serves as a container for shader code，allowing it to
 * be submitted to the GPU for execution within a pipeline.
 */
class ShaderModule {
 public:
  virtual ~ShaderModule() = default;
};
}  // namespace tgfx
