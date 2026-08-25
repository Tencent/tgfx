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

enum class ShaderStageType { Vertex, Fragment };

struct CompileResult {
  bool success = false;
  std::vector<uint32_t> spirv;
  std::string glsl;
  std::string msl;
  std::string wgsl;
  std::string error;
};

/// Compiles GLSL source (already with #defines prepended) to SPIR-V using shaderc.
/// When optimize is true, applies performance-level optimization to reduce SPIR-V size.
/// When openGLEnv is true, targets OpenGL semantics instead of Vulkan: required for
/// sampler2DRect (OpTypeImage Dim=Rect), which is invalid under Vulkan semantics. The
/// resulting SPIR-V must only be translated to GLSL (never Vulkan/Metal).
CompileResult CompileGLSL(const std::string& source, ShaderStageType stage,
                          const std::string& shaderName, uint32_t variantIndex,
                          bool optimize = false, bool openGLEnv = false);

/// Translates SPIR-V binary to desktop GLSL 150 via spirv-cross.
CompileResult TranslateToGLSL(const std::vector<uint32_t>& spirv);

/// Translates SPIR-V binary to Metal Shading Language via spirv-cross.
CompileResult TranslateToMSL(const std::vector<uint32_t>& spirv, ShaderStageType stage);

/// Converts #define-expanded template GLSL to WGSL: preprocess into Vulkan GLSL with
/// separated texture/sampler bindings, compile to SPIR-V via shaderc, then translate to
/// WGSL via tint. Binding contract (must match WebGPURenderPipeline and the runtime JIT
/// path in WebGPUShaderModule): group 0; binding 0/1 = vertex/fragment uniform blocks;
/// per sampler i (declaration order): texture binding = 2+2i, sampler binding = 3+2i.
CompileResult CompileGLSLToWGSL(const std::string& source, ShaderStageType stage,
                                const std::string& shaderName, uint32_t variantIndex);

/// Compiles MSL source text to Metal library binary (.metallib) using xcrun metal/metallib.
/// Returns empty vector on failure.
std::vector<uint8_t> CompileMSLToMetallib(const std::string& mslSource, ShaderStageType stage);

/// Prepends #define directives to shader source from a list of "NAME=value" strings.
std::string PrependDefines(const std::string& source, const std::vector<std::string>& defines);

}  // namespace tgfx
