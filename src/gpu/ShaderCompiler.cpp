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

// Replace a regex pattern in the source string by invoking a callback for each match.
// The callback receives the match and returns the replacement string.
using MatchReplacer = std::string (*)(const std::smatch&, int&);

static std::string replaceAllMatches(const std::string& source, const std::regex& pattern,
                                     MatchReplacer replacer, int& counter) {
  std::smatch match;
  std::string::const_iterator searchStart(source.cbegin());
  std::string result;
  size_t lastPos = 0;
  while (std::regex_search(searchStart, source.cend(), match, pattern)) {
    auto matchPos = static_cast<size_t>(match.position(0));
    auto iterOffset = static_cast<size_t>(searchStart - source.cbegin());
    size_t matchStart = matchPos + iterOffset;
    result += source.substr(lastPos, matchStart - lastPos);
    result += replacer(match, counter);
    lastPos = matchStart + static_cast<size_t>(match.length(0));
    searchStart = match.suffix().first;
  }
  result += source.substr(lastPos);
  return result;
}

static std::string upgradeGLSLVersion(const std::string& source) {
  static std::regex versionRegex(R"(#version\s+\d+(\s+es)?)");
  return std::regex_replace(source, versionRegex, "#version 450");
}

// Assign fixed binding points for internal UBOs to match CPU-side constants:
// VertexUniformBlock -> binding 0 (VERTEX_UBO_BINDING_POINT)
// FragmentUniformBlock -> binding 1 (FRAGMENT_UBO_BINDING_POINT)
static std::string assignInternalUBOBindings(const std::string& source) {
  static std::regex vertexUboRegex(R"(layout\s*\(\s*std140\s*\)\s*uniform\s+VertexUniformBlock)");
  auto result = std::regex_replace(source, vertexUboRegex,
                                   "layout(std140, binding=0) uniform VertexUniformBlock");
  static std::regex fragmentUboRegex(
      R"(layout\s*\(\s*std140\s*\)\s*uniform\s+FragmentUniformBlock)");
  return std::regex_replace(result, fragmentUboRegex,
                            "layout(std140, binding=1) uniform FragmentUniformBlock");
}

static std::string replaceCustomUBO(const std::smatch& match, int& counter) {
  return "layout(std140, binding=" + std::to_string(counter++) + ") uniform " + match[1].str();
}

// Add binding to any remaining uniform blocks (custom shaders) sequentially from 0.
static std::string assignCustomUBOBindings(const std::string& source) {
  static std::regex uboRegex(R"(layout\s*\(\s*std140\s*\)\s*uniform\s+(\w+))");
  int binding = 0;
  return replaceAllMatches(source, uboRegex, replaceCustomUBO, binding);
}

static std::string replaceSamplerBinding(const std::smatch& match, int& counter) {
  return "layout(binding=" + std::to_string(counter++) + ") uniform " + match[1].str() + " " +
         match[2].str() + ";";
}

// Add binding to sampler uniforms sequentially from TEXTURE_BINDING_POINT_START.
// This aligns with the descriptor set layout where bindings 0 and 1 are reserved for UBOs.
static std::string assignSamplerBindings(const std::string& source) {
  static std::regex samplerRegex(R"(uniform\s+(sampler\w+)\s+(\w+);)");
  int binding = TEXTURE_BINDING_POINT_START;
  return replaceAllMatches(source, samplerRegex, replaceSamplerBinding, binding);
}

static std::string replaceInputLocation(const std::smatch& match, int& counter) {
  std::string interpStr = match[1].matched ? match[1].str() : "";
  std::string precisionStr = match[2].matched ? match[2].str() : "";
  return "layout(location=" + std::to_string(counter++) + ") " + interpStr + "in " + precisionStr +
         match[3].str() + " " + match[4].str() + ";";
}

// Add location qualifiers to 'in' variables, handling optional interpolation qualifiers
// (flat, noperspective) and precision qualifiers (highp, mediump, lowp).
static std::string assignInputLocationQualifiers(const std::string& source) {
  static std::regex inVarRegex(
      R"((flat\s+|noperspective\s+)?in\s+(highp\s+|mediump\s+|lowp\s+)?(\w+)\s+(\w+)\s*;)");
  int location = 0;
  return replaceAllMatches(source, inVarRegex, replaceInputLocation, location);
}

static std::string replaceOutputLocation(const std::smatch& match, int& counter) {
  std::string interpStr = match[1].matched ? match[1].str() : "";
  std::string precisionStr = match[2].matched ? match[2].str() : "";
  return "layout(location=" + std::to_string(counter++) + ") " + interpStr + "out " + precisionStr +
         match[3].str() + " " + match[4].str() + ";";
}

// Add location qualifiers to 'out' variables, handling optional interpolation qualifiers
// (flat, noperspective) and precision qualifiers (highp, mediump, lowp).
static std::string assignOutputLocationQualifiers(const std::string& source) {
  static std::regex outVarRegex(
      R"((flat\s+|noperspective\s+)?out\s+(highp\s+|mediump\s+|lowp\s+)?(\w+)\s+(\w+)\s*;)");
  int location = 0;
  return replaceAllMatches(source, outVarRegex, replaceOutputLocation, location);
}

// Remove precision qualifiers that are not supported in desktop GLSL 450.
static std::string removePrecisionDeclarations(const std::string& source) {
  static std::regex precisionDeclRegex(R"(precision\s+(highp|mediump|lowp)\s+\w+\s*;)");
  return std::regex_replace(source, precisionDeclRegex, "");
}

std::string PreprocessGLSL(const std::string& glslCode) {
  auto result = upgradeGLSLVersion(glslCode);
  result = assignInternalUBOBindings(result);
  result = assignCustomUBOBindings(result);
  result = assignSamplerBindings(result);
  result = assignInputLocationQualifiers(result);
  result = assignOutputLocationQualifiers(result);
  result = removePrecisionDeclarations(result);
  return result;
}

std::vector<uint32_t> CompileGLSLToSPIRV(const shaderc::Compiler* compiler,
                                         const std::string& glslCode, ShaderStage stage) {
  if (compiler == nullptr) {
    return {};
  }
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);

  shaderc_shader_kind shaderKind =
      (stage == ShaderStage::Vertex) ? shaderc_vertex_shader : shaderc_fragment_shader;

  shaderc::SpvCompilationResult spvResult =
      compiler->CompileGlslToSpv(glslCode, shaderKind, "shader", "main", options);

  if (spvResult.GetCompilationStatus() != shaderc_compilation_status_success) {
    LOGE("GLSL to SPIR-V compilation error: %s", spvResult.GetErrorMessage().c_str());
    LOGE("GLSL:\n%s", glslCode.c_str());
    return {};
  }

  return {spvResult.cbegin(), spvResult.cend()};
}

}  // namespace tgfx
