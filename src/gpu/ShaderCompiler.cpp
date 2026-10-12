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

#include "ShaderCompiler.h"
#include <regex>
#include <shaderc/shaderc.hpp>
#include "UniformData.h"
#include "core/utils/Log.h"

namespace tgfx {

static std::string replaceSamplerBinding(const std::smatch& match, int& counter) {
  return "layout(set=" + std::to_string(TEXTURE_DESCRIPTOR_SET) +
         ", binding=" + std::to_string(counter++) + ") uniform " + match[1].str() + " " +
         match[2].str() + ";";
}

// Add binding to sampler uniforms sequentially from 0 in descriptor set 2. Vertex and fragment
// UBOs reside in sets 0 and 1, so texture bindings use an independent namespace.
static std::string assignSamplerBindings(const std::string& source) {
  static std::regex samplerRegex(R"(uniform\s+(sampler\w+)\s+(\w+);)");
  int binding = 0;
  return detail::ReplaceAllMatches(source, samplerRegex, replaceSamplerBinding, binding);
}

std::string PreprocessGLSL(const std::string& glslCode, ShaderStage stage) {
  auto result = detail::UpgradeGLSLVersion(glslCode);
  result = detail::AssignInternalUBOBindings(result);
  result = detail::AssignCustomUBOBindings(result, stage);
  result = assignSamplerBindings(result);
  result = detail::AssignInputLocationQualifiers(result, stage);
  result = detail::AssignOutputLocationQualifiers(result, stage);
  result = detail::RemovePrecisionDeclarations(result);
  return result;
}

std::vector<uint32_t> CompileGLSLToSPIRV(const shaderc::Compiler* compiler,
                                         const std::string& glslCode, ShaderStage stage,
                                         bool preserveInterfaceVariables) {
  if (compiler == nullptr) {
    return {};
  }
  shaderc::CompileOptions options;
  // See header doc on `preserveInterfaceVariables` for the rationale; D3D12 requires zero so the
  // optimiser cannot dead-strip fragment inputs that have no body uses, while Vulkan/Metal stay
  // on the performance preset for tighter SPIR-V.
  options.SetOptimizationLevel(preserveInterfaceVariables ? shaderc_optimization_level_zero
                                                          : shaderc_optimization_level_performance);
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);

  shaderc_shader_kind shaderKind =
      (stage == ShaderStage::Vertex) ? shaderc_vertex_shader : shaderc_fragment_shader;

  shaderc::SpvCompilationResult spvResult =
      compiler->CompileGlslToSpv(glslCode, shaderKind, "shader", "main", options);

  if (spvResult.GetCompilationStatus() != shaderc_compilation_status_success) {
    LOGE("GLSL to SPIR-V compilation error: %s", spvResult.GetErrorMessage().c_str());
    LOGE("GLSL (first 512 chars):\n%.512s", glslCode.c_str());
    return {};
  }

  return {spvResult.cbegin(), spvResult.cend()};
}

}  // namespace tgfx
