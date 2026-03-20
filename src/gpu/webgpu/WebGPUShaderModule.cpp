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
#include <map>
#include <regex>
#include <set>
#include <shaderc/shaderc.hpp>
#include "WebGPUGPU.h"
#include "core/utils/Log.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Weffc++"
#include <spirv_msl.hpp>
#include <spirv_parser.hpp>
#pragma clang diagnostic pop

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

static std::string replaceSamplerBinding(const std::smatch& match, int& counter) {
  return "layout(binding=" + std::to_string(counter++) + ") uniform " + match[1].str() + " " +
         match[2].str() + ";";
}

static std::string assignSamplerBindings(const std::string& source) {
  static std::regex samplerRegex(R"(uniform\s+(sampler\w+)\s+(\w+);)");
  int binding = 0;
  return replaceAllMatches(source, samplerRegex, replaceSamplerBinding, binding);
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
  result = assignSamplerBindings(result);
  result = assignInputLocationQualifiers(result);
  result = assignOutputLocationQualifiers(result);
  result = removePrecisionDeclarations(result);
  return result;
}

static std::vector<uint32_t> compileGLSLToSPIRV(const std::string& glslCode, ShaderStage stage) {
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);

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

// Convert SPIR-V binary to MSL string using SPIRV-Cross, then extract WGSL-compatible code.
// Since SPIRV-Cross may not have a WGSL backend in all versions, we use the MSL backend and
// then pass SPIR-V directly to WebGPU via WGPUShaderModuleSPIRVDescriptor (Emscripten extension).
static std::string convertSPIRVToMSL(const std::vector<uint32_t>& spirvBinary, ShaderStage stage) {
  spirv_cross::Parser spvParser(spirvBinary.data(), spirvBinary.size());
  spvParser.parse();

  spirv_cross::CompilerMSL mslCompiler(std::move(spvParser.get_parsed_ir()));

  spirv_cross::CompilerMSL::Options mslOptions;
  mslOptions.set_msl_version(2, 3);
  mslOptions.enable_decoration_binding = true;
  mslCompiler.set_msl_options(mslOptions);

  auto commonOptions = mslCompiler.get_common_options();
  commonOptions.vertex.flip_vert_y = true;
  mslCompiler.set_common_options(commonOptions);

  auto executionModel =
      (stage == ShaderStage::Vertex) ? spv::ExecutionModelVertex : spv::ExecutionModelFragment;

  struct MslBindingInfo {
    uint32_t mslBuffer = 0;
    uint32_t mslTexture = 0;
    uint32_t mslSampler = 0;
  };
  std::map<std::pair<uint32_t, uint32_t>, MslBindingInfo> bindingMap = {};

  auto uboResources = mslCompiler.get_shader_resources().uniform_buffers;
  for (auto& ubo : uboResources) {
    uint32_t spvBinding = mslCompiler.get_decoration(ubo.id, spv::DecorationBinding);
    uint32_t spvDescSet = mslCompiler.get_decoration(ubo.id, spv::DecorationDescriptorSet);
    auto key = std::make_pair(spvDescSet, spvBinding);
    bindingMap[key].mslBuffer = spvBinding;
  }

  auto sampledImages = mslCompiler.get_shader_resources().sampled_images;
  for (auto& image : sampledImages) {
    uint32_t spvBinding = mslCompiler.get_decoration(image.id, spv::DecorationBinding);
    uint32_t spvDescSet = mslCompiler.get_decoration(image.id, spv::DecorationDescriptorSet);
    auto key = std::make_pair(spvDescSet, spvBinding);
    bindingMap[key].mslTexture = spvBinding;
    bindingMap[key].mslSampler = spvBinding;
  }

  for (auto& [key, info] : bindingMap) {
    spirv_cross::MSLResourceBinding resourceBinding = {};
    resourceBinding.stage = executionModel;
    resourceBinding.desc_set = key.first;
    resourceBinding.binding = key.second;
    resourceBinding.msl_buffer = info.mslBuffer;
    resourceBinding.msl_texture = info.mslTexture;
    resourceBinding.msl_sampler = info.mslSampler;
    mslCompiler.add_msl_resource_binding(resourceBinding);
  }

  std::string mslCode = mslCompiler.compile();
  return mslCode;
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

  // Use Emscripten's SPIR-V extension to pass SPIR-V directly to WebGPU.
  WGPUShaderModuleSPIRVDescriptor spirvDesc = {};
  spirvDesc.chain.sType = WGPUSType_ShaderModuleSPIRVDescriptor;
  spirvDesc.codeSize = static_cast<uint32_t>(spirvBinary.size());
  spirvDesc.code = spirvBinary.data();

  WGPUShaderModuleDescriptor moduleDesc = {};
  moduleDesc.nextInChain = &spirvDesc.chain;

  shaderModule = wgpuDeviceCreateShaderModule(device, &moduleDesc);
  if (shaderModule == nullptr) {
    LOGE("WebGPU shader module creation failed");
    return false;
  }
  return true;
}

std::string WebGPUShaderModule::ConvertGLSLToWGSL(const std::string& glslCode,
                                                    ShaderStage stage) {
  std::string vulkanGLSL = preprocessGLSL(glslCode);
  auto spirvBinary = compileGLSLToSPIRV(vulkanGLSL, stage);
  if (spirvBinary.empty()) {
    return "";
  }
  return convertSPIRVToMSL(spirvBinary, stage);
}

void WebGPUShaderModule::onRelease(WebGPUGPU*) {
  if (shaderModule != nullptr) {
    wgpuShaderModuleRelease(shaderModule);
    shaderModule = nullptr;
  }
}

}  // namespace tgfx
