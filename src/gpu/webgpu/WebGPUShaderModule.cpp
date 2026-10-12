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
#include "WebGPUDefines.h"
#include "WebGPUGPU.h"
#include "core/utils/Log.h"
#include "gpu/ShaderCompiler.h"
#include "gpu/UniformData.h"
#include "src/tint/lang/spirv/reader/reader.h"
#include "src/tint/lang/wgsl/writer/writer.h"

namespace tgfx {

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
  auto textureDecl = "layout(set=" + std::to_string(TEXTURE_DESCRIPTOR_SET) +
                     ", binding=" + std::to_string(counter++) + ") uniform " + textureType + " " +
                     name + ";";
  auto samplerDecl = "\nlayout(set=" + std::to_string(TEXTURE_DESCRIPTOR_SET) +
                     ", binding=" + std::to_string(counter++) + ") uniform sampler " + name +
                     "_Sampler;";
  return textureDecl + samplerDecl;
}

static std::string separateSamplerDeclarations(const std::string& source) {
  static std::regex samplerRegex(R"(uniform\s+(sampler\w+)\s+(\w+);)");
  int binding = TEXTURE_BINDING_POINT_START;
  return detail::ReplaceAllMatches(source, samplerRegex, replaceSeparatedSampler, binding);
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

static std::string preprocessGLSL(const std::string& glslCode, ShaderStage stage) {
  auto result = detail::UpgradeGLSLVersion(glslCode);
  result = detail::AssignInternalUBOBindings(result);
  result = detail::AssignCustomUBOBindings(result, stage);
  // Collect sampler names before separating declarations.
  auto samplerNames = collectSamplerNames(result);
  result = separateSamplerDeclarations(result);
  result = separateTextureLookups(result, samplerNames);
  result = detail::AssignInputLocationQualifiers(result, stage);
  result = detail::AssignOutputLocationQualifiers(result, stage);
  result = detail::RemovePrecisionDeclarations(result);
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
    : VaryingShaderModule(ExtractVaryingDecls(descriptor.code, descriptor.stage), {}),
      _stage(descriptor.stage) {
  std::string vulkanGLSL = preprocessGLSL(descriptor.code, descriptor.stage);
  setUniformSlots(detail::CollectUniformSlots(vulkanGLSL));
  compileShader(gpu->device(), vulkanGLSL, descriptor.stage);
}

bool WebGPUShaderModule::compileShader(WGPUDevice device, const std::string& vulkanGLSL,
                                       ShaderStage stage) {
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
    LOGE("  [glsl] %s", vulkanGLSL.c_str());
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
