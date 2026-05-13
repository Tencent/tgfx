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

#include <cstdint>
#include <string>
#include <vector>
#include "tgfx/gpu/ShaderStage.h"

namespace shaderc {
class Compiler;
}

namespace tgfx {

/// Preprocesses OpenGL-style GLSL source code to Vulkan-compatible GLSL 450 with explicit
/// binding/location qualifiers. This includes upgrading the #version directive, assigning UBO and
/// sampler bindings, adding input/output location qualifiers, and removing precision declarations.
std::string PreprocessGLSL(const std::string& glslCode);

/// Compiles preprocessed GLSL 450 source to SPIR-V binary using shaderc. Returns an empty vector
/// on failure.
std::vector<uint32_t> CompileGLSLToSPIRV(const shaderc::Compiler* compiler,
                                         const std::string& vulkanGLSL, ShaderStage stage);

}  // namespace tgfx
