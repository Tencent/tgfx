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

#include <string>
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {
/**
 * ShaderModuleDescriptor describes the properties required to create a ShaderModule.
 */
class ShaderModuleDescriptor {
 public:
  /**
   * The shader code to be compiled into a ShaderModule.
   */
  std::string code;

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
