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

#include "GlslToSpv.h"

#include <shaderc/shaderc.hpp>

#include "gpu/GlslPreprocessor.h"

namespace tgfx {

GlslToSpvResult CompileGlslToSpv(const std::string& glslSource, GlslStage stage) {
  GlslToSpvResult result;
  // Run the same Vulkan-style preprocessing that MetalShaderModule uses — version bump,
  // binding/location injection, precision stripping — so the input to shaderc is identical.
  std::string vulkanGlsl = PreprocessGLSLForVulkan(glslSource);

  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  // Disable performance optimization for the offline path so the generated SPIR-V keeps its
  // debug OpName decorations. Those names drive the identifiers emitted by SPIRV-Cross for
  // cbuffer members / samplers / attribute structs, which is what the UE manifest relies on to
  // bind parameters. Optimization is UE's responsibility via its own ShaderCompilerWorker.
  options.SetOptimizationLevel(shaderc_optimization_level_zero);
  options.SetGenerateDebugInfo();
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);

  shaderc_shader_kind shaderKind =
      (stage == GlslStage::Vertex) ? shaderc_vertex_shader : shaderc_fragment_shader;

  auto spvResult = compiler.CompileGlslToSpv(vulkanGlsl, shaderKind, "shader", "main", options);
  if (spvResult.GetCompilationStatus() != shaderc_compilation_status_success) {
    result.errorMessage = spvResult.GetErrorMessage();
    return result;
  }
  result.spirv.assign(spvResult.cbegin(), spvResult.cend());
  return result;
}

}  // namespace tgfx
