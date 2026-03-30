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

#include "WebGPUShaderModule.h"
#include <regex>
#include <shaderc/shaderc.hpp>
#include "WebGPUGPU.h"
#include "core/utils/Log.h"
#include "gpu/UniformData.h"
#include "src/tint/lang/spirv/reader/reader.h"
#include "src/tint/lang/wgsl/writer/writer.h"

namespace tgfx {

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

static std::string assignCustomUBOBindings(const std::string& source) {
  static std::regex uboRegex(R"(layout\s*\(\s*std140\s*\)\s*uniform\s+(\w+))");
  int binding = 0;
  return replaceAllMatches(source, uboRegex, replaceCustomUBO, binding);
}

// Separates combined sampler declarations into distinct texture and sampler resources.
// Input:  "uniform sampler2D TextureSampler_0;"
// Output: "layout(binding=N) uniform texture2D TextureSampler_0;
//          layout(binding=N+1) uniform sampler TextureSampler_0_Sampler;"
static std::string replaceSeparatedSampler(const std::smatch& match, int& counter) {
  auto samplerType = match[1].str();
  auto name = match[2].str();
  std::string textureType;
  if (samplerType == "sampler2D") {
    textureType = "texture2D";
  } else if (samplerType == "sampler2DRect") {
    textureType = "texture2DRect";
  } else {
    textureType = "texture2D";
  }
  auto textureDecl =
      "layout(binding=" + std::to_string(counter++) + ") uniform " + textureType + " " + name + ";";
  auto samplerDecl =
      "\nlayout(binding=" + std::to_string(counter++) + ") uniform sampler " + name + "_Sampler;";
  return textureDecl + samplerDecl;
}

static std::string separateSamplerDeclarations(const std::string& source) {
  static std::regex samplerRegex(R"(uniform\s+(sampler\w+)\s+(\w+);)");
  int binding = TEXTURE_BINDING_POINT_START;
  return replaceAllMatches(source, samplerRegex, replaceSeparatedSampler, binding);
}

// Replaces texture lookup calls to use separated texture and sampler resources.
// Input:  "texture(TextureSampler_0, coord)"
// Output: "texture(sampler2D(TextureSampler_0, TextureSampler_0_Sampler), coord)"
static std::string separateTextureLookups(const std::string& source,
                                          const std::vector<std::string>& samplerNames) {
  auto result = source;
  for (const auto& name : samplerNames) {
    std::regex lookupRegex("texture\\(" + name + "\\s*,");
    std::string replacement = "texture(sampler2D(" + name + ", " + name + "_Sampler),";
    result = std::regex_replace(result, lookupRegex, replacement);
  }
  return result;
}

// Collects sampler variable names from GLSL source.
static std::vector<std::string> collectSamplerNames(const std::string& source) {
  static std::regex samplerRegex(R"(uniform\s+sampler\w+\s+(\w+);)");
  std::vector<std::string> names = {};
  std::smatch match;
  std::string::const_iterator searchStart(source.cbegin());
  while (std::regex_search(searchStart, source.cend(), match, samplerRegex)) {
    names.push_back(match[1].str());
    searchStart = match.suffix().first;
  }
  return names;
}

static std::string replaceInputLocation(const std::smatch& match, int& counter) {
  std::string interpStr = match[1].matched ? match[1].str() : "";
  std::string precisionStr = match[2].matched ? match[2].str() : "";
  return "layout(location=" + std::to_string(counter++) + ") " + interpStr + "in " + precisionStr +
         match[3].str() + " " + match[4].str() + ";";
}

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

static std::string assignOutputLocationQualifiers(const std::string& source) {
  static std::regex outVarRegex(
      R"((flat\s+|noperspective\s+)?out\s+(highp\s+|mediump\s+|lowp\s+)?(\w+)\s+(\w+)\s*;)");
  int location = 0;
  return replaceAllMatches(source, outVarRegex, replaceOutputLocation, location);
}

static std::string removePrecisionDeclarations(const std::string& source) {
  static std::regex precisionDeclRegex(R"(precision\s+(highp|mediump|lowp)\s+\w+\s*;)");
  return std::regex_replace(source, precisionDeclRegex, "");
}

static std::string preprocessGLSL(const std::string& glslCode) {
  auto result = upgradeGLSLVersion(glslCode);
  result = assignInternalUBOBindings(result);
  result = assignCustomUBOBindings(result);
  // Collect sampler names before separating declarations.
  auto samplerNames = collectSamplerNames(result);
  result = separateSamplerDeclarations(result);
  result = separateTextureLookups(result, samplerNames);
  result = assignInputLocationQualifiers(result);
  result = assignOutputLocationQualifiers(result);
  result = removePrecisionDeclarations(result);
  return result;
}

static std::vector<uint32_t> compileGLSLToSPIRV(const std::string& glslCode, ShaderStage stage) {
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);

  // Enable auto binding to help with texture/sampler separation
  options.SetAutoBindUniforms(true);
  options.SetAutoMapLocations(true);

  shaderc_shader_kind shaderKind =
      (stage == ShaderStage::Vertex) ? shaderc_vertex_shader : shaderc_fragment_shader;

  shaderc::SpvCompilationResult spvResult =
      compiler.CompileGlslToSpv(glslCode, shaderKind, "shader", "main", options);

  if (spvResult.GetCompilationStatus() != shaderc_compilation_status_success) {
    LOGE("GLSL to SPIR-V compilation error: %s", spvResult.GetErrorMessage().c_str());
    LOGE("GLSL:\n%s", glslCode.c_str());
    return {};
  }

  return {spvResult.cbegin(), spvResult.cend()};
}

std::shared_ptr<WebGPUShaderModule> WebGPUShaderModule::Make(
    WebGPUGPU* gpu, const ShaderModuleDescriptor& descriptor) {
  if (gpu == nullptr) {
    return nullptr;
  }
  return gpu->makeResource<WebGPUShaderModule>(gpu, descriptor);
}

WebGPUShaderModule::WebGPUShaderModule(WebGPUGPU* gpu, const ShaderModuleDescriptor& descriptor)
    : _stage(descriptor.stage),
      _glslCode(descriptor.stage == ShaderStage::Fragment ? descriptor.code : std::string{}) {
  compileShader(gpu->device(), descriptor.code, descriptor.stage);
}

bool WebGPUShaderModule::compileShader(WGPUDevice device, const std::string& glslCode,
                                       ShaderStage stage) {
  std::string vulkanGLSL = preprocessGLSL(glslCode);
  auto spirvBinary = compileGLSLToSPIRV(vulkanGLSL, stage);
  if (spirvBinary.empty()) {
    return false;
  }

  // Convert SPIR-V to WGSL using Tint.
  tint::spirv::reader::Options readerOptions;
  readerOptions.allow_non_uniform_derivatives = true;
  tint::Program program = tint::spirv::reader::Read(spirvBinary, readerOptions);
  if (!program.IsValid()) {
    LOGE("Tint SPIR-V reader failed (%zu words, magic=0x%08x):", spirvBinary.size(),
         spirvBinary.size() > 0 ? spirvBinary[0] : 0);
    for (const auto& diag : program.Diagnostics()) {
      LOGE("  [tint] %s", diag.message.Plain().c_str());
    }
    LOGE("  [glsl] %s", glslCode.c_str());
    return false;
  }

  tint::wgsl::writer::Options writerOptions;
  auto result = tint::wgsl::writer::Generate(program, writerOptions);
  if (result != tint::Success) {
    LOGE("Tint WGSL writer failed to generate WGSL");
    return false;
  }

  std::string wgslCode = result->wgsl;
  if (wgslCode.empty()) {
    LOGE("Tint generated empty WGSL code");
    return false;
  }

  // Create shader module with WGSL code.
  WGPUShaderModuleWGSLDescriptor wgslDescriptor = {};
  wgslDescriptor.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
  wgslDescriptor.code = wgslCode.c_str();

  WGPUShaderModuleDescriptor moduleDesc = {};
  moduleDesc.nextInChain = &wgslDescriptor.chain;

  shaderModule = wgpuDeviceCreateShaderModule(device, &moduleDesc);
  if (shaderModule == nullptr) {
    LOGE("WebGPU shader module creation failed");
    return false;
  }
  return true;
}

void WebGPUShaderModule::onRelease(WebGPUGPU*) {
  if (shaderModule != nullptr) {
    wgpuShaderModuleRelease(shaderModule);
    shaderModule = nullptr;
  }
}

}  // namespace tgfx
