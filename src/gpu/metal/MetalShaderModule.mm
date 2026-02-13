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

#include "MetalShaderModule.h"
#include "MetalGPU.h"
#include "core/utils/Log.h"
#include <map>
#include <regex>
#include <set>
#include <shaderc/shaderc.hpp>
// Suppress warnings from SPIRV-Cross headers
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Weffc++"
#include <spirv_msl.hpp>
#include <spirv_parser.hpp>
#pragma clang diagnostic pop
#include <string>

namespace tgfx {

// TODO: The current GLSL -> SPIR-V -> MSL conversion pipeline incurs high runtime compilation
// overhead. Future improvements may include offline pre-compilation, shader caching, or binary
// pipeline archive support to reduce latency.

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

// Add binding to sampler uniforms sequentially from 0.
// The RenderPipeline maintains a mapping from logical binding to actual Metal texture index.
static std::string assignSamplerBindings(const std::string& source) {
  static std::regex samplerRegex(R"(uniform\s+(sampler\w+)\s+(\w+);)");
  int binding = 0;
  return replaceAllMatches(source, samplerRegex, replaceSamplerBinding, binding);
}

static std::string replaceInputLocation(const std::smatch& match, int& counter) {
  std::string interpStr = match[1].matched ? match[1].str() : "";
  std::string precisionStr = match[2].matched ? match[2].str() : "";
  return "layout(location=" + std::to_string(counter++) + ") " + interpStr + "in " +
         precisionStr + match[3].str() + " " + match[4].str() + ";";
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
  return "layout(location=" + std::to_string(counter++) + ") " + interpStr + "out " +
         precisionStr + match[3].str() + " " + match[4].str() + ";";
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

// Preprocess OpenGL-style GLSL to Vulkan-compatible GLSL with binding/location qualifiers.
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

// Compile GLSL source to SPIR-V binary.
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

// Collect all specialization constant IDs used in the given SPIR-V binary.
static std::set<uint32_t> collectSpecConstantIDs(const std::vector<uint32_t>& spirvBinary) {
  spirv_cross::Parser spvParser(spirvBinary.data(), spirvBinary.size());
  spvParser.parse();
  spirv_cross::Compiler compiler(std::move(spvParser.get_parsed_ir()));
  auto specConstants = compiler.get_specialization_constants();
  std::set<uint32_t> usedIDs = {};
  for (auto& sc : specConstants) {
    usedIDs.insert(sc.constant_id);
  }
  return usedIDs;
}

// Find the smallest non-negative constant_id not in the given set.
static uint32_t findAvailableConstantID(const std::set<uint32_t>& usedIDs) {
  uint32_t id = 0;
  while (usedIDs.count(id) > 0) {
    id++;
  }
  return id;
}

// Inject a specialization constant for sample mask and gl_SampleMask output into preprocessed GLSL.
// The constant_id is chosen to avoid collision with any existing specialization constants.
static std::string injectSampleMask(const std::string& glslCode, uint32_t constantID) {
  std::string result = glslCode;
  std::string sampleMaskDecl = "layout(constant_id = " + std::to_string(constantID) +
                               ") const uint tgfx_SampleMask = 0xFFFFFFFFu;\n";
  auto versionEnd = result.find('\n');
  if (versionEnd != std::string::npos) {
    result.insert(versionEnd + 1, sampleMaskDecl);
  }
  auto lastBrace = result.rfind('}');
  if (lastBrace != std::string::npos) {
    result.insert(lastBrace, "    gl_SampleMask[0] = int(tgfx_SampleMask);\n");
  }
  return result;
}

// Convert a SPIR-V binary to MSL source code.
static std::string convertSPIRVToMSL(const std::vector<uint32_t>& spirvBinary, ShaderStage stage) {
  spirv_cross::Parser spvParser(spirvBinary.data(), spirvBinary.size());
  spvParser.parse();

  spirv_cross::CompilerMSL mslCompiler(std::move(spvParser.get_parsed_ir()));

  spirv_cross::CompilerMSL::Options mslOptions;
  mslOptions.set_msl_version(2, 3);
  mslOptions.enable_decoration_binding = true;
  // Enable framebuffer fetch so SPIRV-Cross converts subpassInput to [[color(0)]] in MSL.
  mslOptions.use_framebuffer_fetch_subpasses = true;
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
  if (mslCode.empty()) {
    LOGE("SPIR-V to MSL conversion failed");
    return "";
  }

  return mslCode;
}

std::shared_ptr<MetalShaderModule> MetalShaderModule::Make(MetalGPU* gpu,
                                                      const ShaderModuleDescriptor& descriptor) {
  if (!gpu) {
    return nullptr;
  }

  return gpu->makeResource<MetalShaderModule>(gpu, descriptor);
}

MetalShaderModule::MetalShaderModule(MetalGPU* gpu, const ShaderModuleDescriptor& descriptor)
    : _stage(descriptor.stage),
      _glslCode(descriptor.stage == ShaderStage::Fragment ? descriptor.code : std::string{}) {
  compileShader(gpu->device(), descriptor.code, descriptor.stage);
}

void MetalShaderModule::onRelease(MetalGPU*) {
  if (library != nil) {
    [library release];
    library = nil;
  }
}

bool MetalShaderModule::compileShader(id<MTLDevice> device, const std::string& glslCode,
                                    ShaderStage stage) {
  std::string mslCode = convertGLSLToMSL(glslCode, stage);
  if (mslCode.empty()) {
    return false;
  }

  NSString* mslSource = [NSString stringWithUTF8String:mslCode.c_str()];
  NSError* error = nil;

  library = [device newLibraryWithSource:mslSource options:nil error:&error];
  if (!library) {
    if (error) {
      LOGE("Metal shader compilation error: %s", error.localizedDescription.UTF8String);
      LOGE("Original GLSL:\n%s", glslCode.c_str());
      LOGE("MSL code:\n%s", mslCode.c_str());
    }
    return false;
  }

  return true;
}

std::string MetalShaderModule::convertGLSLToMSL(const std::string& glslCode, ShaderStage stage) {
  std::string vulkanGLSL = preprocessGLSL(glslCode);
  auto spirvBinary = compileGLSLToSPIRV(vulkanGLSL, stage);
  if (spirvBinary.empty()) {
    return "";
  }
  return convertSPIRVToMSL(spirvBinary, stage);
}

SampleMaskCompileResult CompileFragmentShaderWithSampleMask(id<MTLDevice> device,
                                                            const std::string& glslCode) {
  SampleMaskCompileResult result = {};
  auto stage = ShaderStage::Fragment;
  std::string vulkanGLSL = preprocessGLSL(glslCode);

  // First pass: compile to SPIR-V and collect used specialization constant IDs.
  auto spirvBinary = compileGLSLToSPIRV(vulkanGLSL, stage);
  if (spirvBinary.empty()) {
    return result;
  }
  auto usedIDs = collectSpecConstantIDs(spirvBinary);
  result.constantID = findAvailableConstantID(usedIDs);

  // Second pass: inject sample mask with the chosen constant_id and re-compile.
  std::string injectedGLSL = injectSampleMask(vulkanGLSL, result.constantID);
  auto injectedSPIRV = compileGLSLToSPIRV(injectedGLSL, stage);
  if (injectedSPIRV.empty()) {
    return result;
  }
  std::string mslCode = convertSPIRVToMSL(injectedSPIRV, stage);
  if (mslCode.empty()) {
    return result;
  }

  NSString* mslSource = [NSString stringWithUTF8String:mslCode.c_str()];
  NSError* error = nil;
  result.library = [device newLibraryWithSource:mslSource options:nil error:&error];
  if (!result.library) {
    if (error) {
      LOGE("Metal shader compilation error (sample mask): %s", error.localizedDescription.UTF8String);
    }
    result.library = nil;
  }
  return result;
}

}  // namespace tgfx
