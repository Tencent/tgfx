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

namespace tgfx {

enum class GlslStage {
  Vertex,
  Fragment,
};

/**
 * Result of compiling a TGFX-style GLSL source to a SPIR-V binary. When `errorMessage` is
 * non-empty the compilation failed and `spirv` will be empty.
 */
struct GlslToSpvResult {
  std::vector<uint32_t> spirv;
  std::string errorMessage;
};

/**
 * Compile a GLSL source (any version tgfx produces) to a SPIR-V binary, using the exact same
 * Vulkan-target pipeline that MetalShaderModule uses at runtime. Internally runs
 * PreprocessGLSLForVulkan() and then shaderc with optimizationLevel=performance and
 * target=Vulkan 1.0.
 *
 * Thread-safe across distinct calls; uses a fresh shaderc::Compiler per invocation for simplicity.
 */
GlslToSpvResult CompileGlslToSpv(const std::string& glslSource, GlslStage stage);

}  // namespace tgfx
