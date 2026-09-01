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
#include <algorithm>
#include <regex>
#include <shaderc/shaderc.hpp>
#include <unordered_map>
#include "UniformData.h"
#include "core/utils/Log.h"

namespace tgfx {

static int UniformDescriptorSet(ShaderStage stage) {
  return stage == ShaderStage::Vertex ? VERTEX_UBO_DESCRIPTOR_SET : FRAGMENT_UBO_DESCRIPTOR_SET;
}

// Assign fixed local binding points for internal UBOs. Vertex and fragment UBOs live in separate
// descriptor sets, so their numeric bindings are physical stage-local slots rather than public
// BindingEntry.binding values.
static std::string assignInternalUBOBindings(const std::string& source) {
  static std::regex vertexUboRegex(R"(layout\s*\(\s*std140\s*\)\s*uniform\s+VertexUniformBlock)");
  auto result =
      std::regex_replace(source, vertexUboRegex,
                         "layout(std140, set=" + std::to_string(VERTEX_UBO_DESCRIPTOR_SET) +
                             ", binding=0) uniform VertexUniformBlock");
  static std::regex fragmentUboRegex(
      R"(layout\s*\(\s*std140\s*\)\s*uniform\s+FragmentUniformBlock)");
  return std::regex_replace(result, fragmentUboRegex,
                            "layout(std140, set=" + std::to_string(FRAGMENT_UBO_DESCRIPTOR_SET) +
                                ", binding=0) uniform FragmentUniformBlock");
}

static int nextUniformBinding(const std::string& source, int descriptorSet) {
  static std::regex boundUboRegex(
      R"(layout\s*\(\s*std140\s*,\s*set\s*=\s*(\d+)\s*,\s*binding\s*=\s*(\d+)\s*\)\s*uniform)");
  int nextBinding = 0;
  std::smatch match;
  std::string::const_iterator searchStart(source.cbegin());
  while (std::regex_search(searchStart, source.cend(), match, boundUboRegex)) {
    if (std::stoi(match[1].str()) == descriptorSet) {
      nextBinding = std::max(nextBinding, std::stoi(match[2].str()) + 1);
    }
    searchStart = match.suffix().first;
  }
  return nextBinding;
}

// Add physical bindings to remaining uniform blocks sequentially within the shader stage's
// descriptor set. The public BindingEntry.binding is resolved by name at pipeline creation time.
static std::string assignCustomUBOBindings(const std::string& source, ShaderStage stage) {
  static std::regex uboRegex(R"(layout\s*\(\s*std140\s*\)\s*uniform\s+(\w+))");
  std::smatch match;
  std::string::const_iterator searchStart(source.cbegin());
  std::string result;
  size_t lastPos = 0;
  int binding = nextUniformBinding(source, UniformDescriptorSet(stage));
  while (std::regex_search(searchStart, source.cend(), match, uboRegex)) {
    auto matchPos = static_cast<size_t>(match.position(0));
    auto iterOffset = static_cast<size_t>(searchStart - source.cbegin());
    size_t matchStart = matchPos + iterOffset;
    result += source.substr(lastPos, matchStart - lastPos);
    result += "layout(std140, set=" + std::to_string(UniformDescriptorSet(stage)) +
              ", binding=" + std::to_string(binding++) + ") uniform " + match[1].str();
    lastPos = matchStart + static_cast<size_t>(match.length(0));
    searchStart = match.suffix().first;
  }
  result += source.substr(lastPos);
  return result;
}

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

std::vector<ShaderUniformBinding> GetShaderUniformBindings(const std::string& preprocessedGLSL) {
  static std::regex uboRegex(
      R"(layout\s*\(\s*std140\s*,\s*set\s*=\s*(\d+)\s*,\s*binding\s*=\s*(\d+)\s*\)\s*uniform\s+(\w+))");
  std::vector<ShaderUniformBinding> bindings;
  std::smatch match;
  std::string::const_iterator searchStart(preprocessedGLSL.cbegin());
  while (std::regex_search(searchStart, preprocessedGLSL.cend(), match, uboRegex)) {
    bindings.push_back({match[3].str(), static_cast<uint32_t>(std::stoul(match[1].str())),
                        static_cast<uint32_t>(std::stoul(match[2].str()))});
    searchStart = match.suffix().first;
  }
  return bindings;
}

std::string PreprocessGLSL(const std::string& glslCode, ShaderStage stage) {
  auto result = detail::UpgradeGLSLVersion(glslCode);
  result = assignInternalUBOBindings(result);
  result = assignCustomUBOBindings(result, stage);
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
