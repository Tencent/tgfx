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
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <shaderc/shaderc.hpp>
#include <spirv_glsl.hpp>
#include <spirv_msl.hpp>
#include <spirv_parser.hpp>
#ifdef TGFX_SHADER_TOOL_HAS_TINT
#include "src/tint/lang/spirv/reader/reader.h"
#include "src/tint/lang/wgsl/writer/writer.h"
#endif

namespace tgfx {

#ifdef TGFX_SHADER_TOOL_HAS_TINT
// First binding slot for separated texture/sampler pairs; must match TEXTURE_BINDING_POINT_START
// in src/gpu/webgpu/WebGPUDefines.h and the bind-group expansion in WebGPURenderPipeline.
static constexpr int kWebGPUTextureBindingStart = 2;
#endif

std::string PrependDefines(const std::string& source, const std::vector<std::string>& defines) {
  std::string prefix;
  for (const auto& def : defines) {
    auto eq = def.find('=');
    if (eq != std::string::npos) {
      prefix += "#define " + def.substr(0, eq) + " " + def.substr(eq + 1) + "\n";
    } else {
      prefix += "#define " + def + "\n";
    }
  }
  // Insert defines after the #version line. Search for it since the file may start with comments.
  auto versionPos = source.find("#version");
  if (versionPos != std::string::npos) {
    auto versionEnd = source.find('\n', versionPos);
    if (versionEnd != std::string::npos) {
      return source.substr(0, versionEnd + 1) + prefix + source.substr(versionEnd + 1);
    }
  }
  return prefix + source;
}

CompileResult CompileGLSL(const std::string& source, ShaderStageType stage,
                          const std::string& shaderName, uint32_t variantIndex, bool optimize,
                          bool openGLEnv) {
  CompileResult result;
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(optimize ? shaderc_optimization_level_performance
                                        : shaderc_optimization_level_zero);
  if (openGLEnv) {
    options.SetTargetEnvironment(shaderc_target_env_opengl, shaderc_env_version_opengl_4_5);
  } else {
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
  }

  auto shaderKind =
      (stage == ShaderStageType::Vertex) ? shaderc_vertex_shader : shaderc_fragment_shader;
  std::string filename = shaderName + "_variant_" + std::to_string(variantIndex);

  auto spvResult = compiler.CompileGlslToSpv(source, shaderKind, filename.c_str(), "main", options);

  if (spvResult.GetCompilationStatus() != shaderc_compilation_status_success) {
    result.success = false;
    result.error = "[" + shaderName + " variant " + std::to_string(variantIndex) + "] " +
                   spvResult.GetErrorMessage();
    return result;
  }

  result.success = true;
  result.spirv = {spvResult.cbegin(), spvResult.cend()};
  return result;
}

CompileResult TranslateToGLSL(const std::vector<uint32_t>& spirv) {
  CompileResult result;
  // spirv-cross uses exceptions internally, so isolate them at the third-party API boundary.
  try {
    spirv_cross::CompilerGLSL compiler(spirv);
    auto options = compiler.get_common_options();
    // GLSL 330 keeps layout(location=) on vertex inputs, which the precompiled GL pipeline
    // binds attributes by (name-based binding breaks because template attribute names differ
    // from the runtime GeometryProcessor names). Requires desktop GL 3.3; older drivers fail
    // at module creation and fall back to ProgramBuilder cleanly.
    options.version = 330;
    options.es = false;
    options.vulkan_semantics = false;
    options.enable_420pack_extension = false;
    compiler.set_common_options(options);
    result.glsl = compiler.compile();
    result.success = true;
  } catch (const spirv_cross::CompilerError& e) {
    result.error = std::string("spirv-cross GLSL error: ") + e.what();
  } catch (const std::exception& e) {
    result.error = std::string("spirv-cross exception: ") + e.what();
  }
  return result;
}

CompileResult TranslateToMSL(const std::vector<uint32_t>& spirv, ShaderStageType stage) {
  CompileResult result;
  // spirv-cross uses C++ exceptions internally for error reporting. This catch block is a
  // necessary boundary isolation to convert third-party exceptions into return values, as
  // TGFX project rules forbid exceptions in our own code.
  try {
    spirv_cross::Parser parser(spirv);
    parser.parse();
    spirv_cross::CompilerMSL mslCompiler(std::move(parser.get_parsed_ir()));

    spirv_cross::CompilerMSL::Options mslOptions;
    mslOptions.set_msl_version(2, 3);
    mslOptions.enable_decoration_binding = true;
    if (stage == ShaderStageType::Fragment) {
      mslOptions.use_framebuffer_fetch_subpasses = true;
    }
    mslCompiler.set_msl_options(mslOptions);

    auto commonOptions = mslCompiler.get_common_options();
    commonOptions.vertex.flip_vert_y = true;
    mslCompiler.set_common_options(commonOptions);

    auto executionModel = (stage == ShaderStageType::Vertex) ? spv::ExecutionModelVertex
                                                             : spv::ExecutionModelFragment;

    // Map SPIR-V resource bindings to MSL buffer/texture/sampler indices.
    // This must match the runtime MetalShaderModule::convertSPIRVToMSL logic:
    // mslBuffer = spvBinding, mslTexture = spvBinding, mslSampler = spvBinding.
    auto uboResources = mslCompiler.get_shader_resources().uniform_buffers;
    for (auto& ubo : uboResources) {
      uint32_t spvBinding = mslCompiler.get_decoration(ubo.id, spv::DecorationBinding);
      uint32_t spvDescSet = mslCompiler.get_decoration(ubo.id, spv::DecorationDescriptorSet);
      spirv_cross::MSLResourceBinding binding = {};
      binding.stage = executionModel;
      binding.desc_set = spvDescSet;
      binding.binding = spvBinding;
      binding.msl_buffer = spvBinding;
      mslCompiler.add_msl_resource_binding(binding);
    }

    auto sampledImages = mslCompiler.get_shader_resources().sampled_images;
    for (auto& image : sampledImages) {
      uint32_t spvBinding = mslCompiler.get_decoration(image.id, spv::DecorationBinding);
      uint32_t spvDescSet = mslCompiler.get_decoration(image.id, spv::DecorationDescriptorSet);
      spirv_cross::MSLResourceBinding binding = {};
      binding.stage = executionModel;
      binding.desc_set = spvDescSet;
      binding.binding = spvBinding;
      binding.msl_texture = spvBinding;
      binding.msl_sampler = spvBinding;
      mslCompiler.add_msl_resource_binding(binding);
    }

    // Note: subpass_inputs are NOT mapped here. When use_framebuffer_fetch_subpasses=true,
    // spirv-cross automatically converts subpassInput to [[color(N)]] in MSL without
    // needing explicit resource binding.

    result.msl = mslCompiler.compile();
    result.success = true;

  } catch (const spirv_cross::CompilerError& e) {
    result.success = false;
    result.error = std::string("spirv-cross MSL error: ") + e.what();
  } catch (const std::exception& e) {
    result.success = false;
    result.error = std::string("spirv-cross exception: ") + e.what();
  }
  return result;
}

#ifdef TGFX_SHADER_TOOL_HAS_TINT
// Rebuilds a string by applying a regex-driven replacer over successive matches, passing a
// mutable counter so replacers can allocate sequential binding numbers.
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

// Template shaders declare combined image samplers in set 1. The binding expression may be a
// literal, a preprocessor macro, or a parenthesized macro expansion like "(4 + 1)", so it must
// tolerate one level of nested parentheses. The original numbers are ignored: bindings are
// reassigned in declaration order, mirroring the runtime JIT path
// (WebGPUShaderModule::preprocessGLSL) and the WebGPURenderPipeline bind-group expansion.
static const char* kWebGPUSamplerDeclRegex =
    R"(layout\s*\(\s*set\s*=\s*1\s*,\s*binding\s*=\s*(?:[^()]|\([^)]*\))*\s*\)\s*uniform\s+(?:sampler2D|CHAIN_LEAF_SAMPLER)\s+(\w+)\s*;)";

static std::vector<std::string> collectSamplerNames(const std::string& source) {
  static std::regex samplerRegex(kWebGPUSamplerDeclRegex);
  std::vector<std::string> names = {};
  std::smatch match;
  std::string::const_iterator searchStart(source.cbegin());
  while (std::regex_search(searchStart, source.cend(), match, samplerRegex)) {
    names.push_back(match[1].str());
    searchStart = match.suffix().first;
  }
  return names;
}

static std::string replaceSeparatedSampler(const std::smatch& match, int& counter) {
  const auto& name = match[1].str();
  auto textureDecl = "layout(set = 0, binding = " + std::to_string(counter++) +
                     ") uniform texture2D " + name + ";";
  auto samplerDecl = "\nlayout(set = 0, binding = " + std::to_string(counter++) +
                     ") uniform sampler " + name + "_Sampler;";
  return textureDecl + samplerDecl;
}

// Rewrites combined-sampler lookups to construct the combined sampler from its separated
// texture and sampler resources, which Vulkan GLSL supports directly.
static std::string rewriteTextureLookups(const std::string& source,
                                         const std::vector<std::string>& samplerNames) {
  auto result = source;
  for (const auto& name : samplerNames) {
    std::regex lookupRegex("texture\\(\\s*" + name + "\\s*,");
    result = std::regex_replace(result, lookupRegex,
                                "texture(sampler2D(" + name + ", " + name + "_Sampler),");
  }
  return result;
}

// WGSL has no combined image samplers, so functions taking one as a parameter need their
// signature split into separate texture and sampler parameters, and every call site needs the
// matching sampler argument appended. Only the leading-parameter form is supported; a sampler
// in any other parameter position fails to match, surfaces as a shaderc error, and must be
// handled by extending this pass.
struct SamplerParamFunction {
  std::string funcName;
  std::string paramName;
};

static std::vector<SamplerParamFunction> collectSamplerParamFunctions(
    const std::string& source) {
  static std::regex sigRegex(R"(\w+\s+(\w+)\s*\(\s*(?:CHAIN_LEAF_SAMPLER|sampler2D)\s+(\w+)\s*,)");
  std::vector<SamplerParamFunction> functions = {};
  std::smatch match;
  std::string::const_iterator searchStart(source.cbegin());
  while (std::regex_search(searchStart, source.cend(), match, sigRegex)) {
    functions.push_back({match[1].str(), match[2].str()});
    searchStart = match.suffix().first;
  }
  return functions;
}

static std::string replaceSplitSamplerParam(const std::smatch& match, int& /*counter*/) {
  const auto& name = match[1].str();
  return "texture2D " + name + ", sampler " + name + "_Sampler,";
}

static std::string splitSamplerParamSignatures(const std::string& source) {
  static std::regex paramRegex(R"((?:CHAIN_LEAF_SAMPLER|sampler2D)\s+(\w+)\s*,)");
  int unused = 0;
  return replaceAllMatches(source, paramRegex, replaceSplitSamplerParam, unused);
}

// Expands the leading combined-sampler argument at call sites of sampler-param functions:
// "func(Tex, ..." becomes "func(Tex, Tex_Sampler, ...". The argument name must already refer to
// a known combined sampler (a top-level uniform or another sampler function parameter).
static std::string expandSamplerCallArgs(const std::string& source,
                                         const std::vector<SamplerParamFunction>& functions,
                                         const std::vector<std::string>& combinedNames) {
  auto result = source;
  for (const auto& function : functions) {
    std::regex callRegex("(" + function.funcName + R"(\s*\(\s*)(\w+))");
    std::smatch match;
    std::string::const_iterator searchStart(result.cbegin());
    std::string expanded;
    size_t lastPos = 0;
    const std::string& subject = result;
    while (std::regex_search(searchStart, subject.cend(), match, callRegex)) {
      auto matchPos = static_cast<size_t>(match.position(0));
      auto iterOffset = static_cast<size_t>(searchStart - subject.cbegin());
      size_t matchStart = matchPos + iterOffset;
      expanded += subject.substr(lastPos, matchStart - lastPos);
      auto argName = match[2].str();
      if (std::find(combinedNames.begin(), combinedNames.end(), argName) != combinedNames.end()) {
        expanded += match[1].str() + argName + ", " + argName + "_Sampler";
      } else {
        expanded += match.str();
      }
      lastPos = matchStart + static_cast<size_t>(match.length(0));
      searchStart = match.suffix().first;
    }
    expanded += subject.substr(lastPos);
    result = expanded;
  }
  return result;
}

// Runs the GLSL preprocessor before the regex passes. Template shaders wrap conditional sampler
// declarations in #if blocks (e.g. UnifiedGradientShader's GradientTexture), so a regex pass over
// the raw source would count declarations the preprocessor removes, leaving binding holes between
// the reassigned binding numbers and the reflection's sampler list. Preprocessing first keeps the
// declaration set exact.
static std::string preprocessGLSLMacros(const std::string& source, ShaderStageType stage,
                                        std::string* error) {
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  auto shaderKind =
      (stage == ShaderStageType::Vertex) ? shaderc_vertex_shader : shaderc_fragment_shader;
  auto preprocessed = compiler.PreprocessGlsl(source, shaderKind, "shader", options);
  if (preprocessed.GetCompilationStatus() != shaderc_compilation_status_success) {
    *error = preprocessed.GetErrorMessage();
    return {};
  }
  std::string result{preprocessed.cbegin(), preprocessed.cend()};
  // Strip the #line directives the preprocessor emits: they can appear between a layout
  // qualifier and the uniform declaration, breaking the regex passes below.
  static std::regex lineRegex(R"((^|\n)[ \t]*#[ \t]*line[^\n]*)");
  result = std::regex_replace(result, lineRegex, "$1");
  return result;
}

static std::string preprocessWebGPU(const std::string& source) {
  auto samplerNames = collectSamplerNames(source);
  auto samplerParamFunctions = collectSamplerParamFunctions(source);
  auto result = splitSamplerParamSignatures(source);
  // Function bodies index the combined sampler through the parameter name, so parameter names
  // join the uniform names for texture() rewriting.
  auto lookupNames = samplerNames;
  for (const auto& function : samplerParamFunctions) {
    lookupNames.push_back(function.paramName);
  }
  static std::regex samplerDeclRegex(kWebGPUSamplerDeclRegex);
  int binding = kWebGPUTextureBindingStart;
  result = replaceAllMatches(result, samplerDeclRegex, replaceSeparatedSampler, binding);
  result = rewriteTextureLookups(result, lookupNames);
  return expandSamplerCallArgs(result, samplerParamFunctions, lookupNames);
}

static std::vector<uint32_t> compileSPIRVForWGSL(const std::string& source, ShaderStageType stage,
                                                 std::string* error) {
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
  auto shaderKind =
      (stage == ShaderStageType::Vertex) ? shaderc_vertex_shader : shaderc_fragment_shader;
  auto spvResult = compiler.CompileGlslToSpv(source, shaderKind, "shader", "main", options);
  if (spvResult.GetCompilationStatus() != shaderc_compilation_status_success) {
    *error = spvResult.GetErrorMessage();
    return {};
  }
  return {spvResult.cbegin(), spvResult.cend()};
}
#endif

CompileResult CompileGLSLToWGSL(const std::string& source, ShaderStageType stage,
                                const std::string& shaderName, uint32_t variantIndex) {
  CompileResult result;
  std::string errorPrefix = "[" + shaderName + " variant " + std::to_string(variantIndex) + "] ";
#ifdef TGFX_SHADER_TOOL_HAS_TINT
  std::string compileError;
  auto expandedSource = preprocessGLSLMacros(source, stage, &compileError);
  if (expandedSource.empty()) {
    result.error = errorPrefix + "GLSL preprocess: " + compileError;
    return result;
  }
  auto vulkanGLSL = preprocessWebGPU(expandedSource);
  if (const char* dumpDir = std::getenv("TGFX_SHADER_DUMP_DIR")) {
    static std::atomic<int> dumpCounter{0};
    std::string base = std::string(dumpDir) + "/wgsl_" + shaderName + "_v" +
                       std::to_string(variantIndex) + "_" + std::to_string(dumpCounter++);
    FILE* f = fopen((base + ".pre.glsl").c_str(), "w");
    if (f) {
      fwrite(expandedSource.data(), 1, expandedSource.size(), f);
      fclose(f);
    }
    f = fopen((base + ".final.glsl").c_str(), "w");
    if (f) {
      fwrite(vulkanGLSL.data(), 1, vulkanGLSL.size(), f);
      fclose(f);
    }
  }
  auto spirv = compileSPIRVForWGSL(vulkanGLSL, stage, &compileError);
  if (spirv.empty()) {
    result.error = errorPrefix + "GLSL to SPIR-V: " + compileError;
    return result;
  }
  // The reader options mirror WebGPUShaderModule::compileShader so the AOT and JIT paths
  // produce equivalent WGSL from the same shader sources.
  tint::spirv::reader::Options readerOptions;
  readerOptions.allow_non_uniform_derivatives = true;
  auto program = tint::spirv::reader::Read(spirv, readerOptions);
  if (!program.IsValid()) {
    std::string diagnostics;
    for (const auto& diag : program.Diagnostics()) {
      diagnostics += " " + diag.message.Plain();
    }
    result.error = errorPrefix + "tint SPIR-V reader:" + diagnostics;
    return result;
  }
  tint::wgsl::writer::Options writerOptions;
  auto wgslResult = tint::wgsl::writer::Generate(program, writerOptions);
  if (wgslResult != tint::Success) {
    result.error = errorPrefix + "tint WGSL writer failed to generate WGSL";
    return result;
  }
  if (wgslResult->wgsl.empty()) {
    result.error = errorPrefix + "tint generated empty WGSL code";
    return result;
  }
  result.wgsl = std::move(wgslResult->wgsl);
  result.success = true;
  return result;
#else
  // Unreachable in practice: the CMake wiring defines TGFX_SHADER_TOOL_HAS_TINT exactly when
  // "webgpu" can appear in --backends. Kept explicit so a misconfigured build fails loudly
  // instead of silently emitting an empty WebGPU bundle.
  (void)source;
  (void)stage;
  result.error = errorPrefix +
                 "WebGPU translation is unavailable: build with -DTGFX_BUILD_WEBGPU_BUNDLE=ON";
  return result;
#endif
}

std::vector<uint8_t> CompileMSLToMetallib(const std::string& mslSource, ShaderStageType stage) {
  // Use xcrun metal to compile MSL to AIR, then xcrun metallib to produce .metallib binary.
  // Thread-safe via unique temp file names using the address of the source string.
  auto addr = reinterpret_cast<uintptr_t>(&mslSource);
  char tmpMsl[256];
  char tmpAir[256];
  char tmpLib[256];
  snprintf(tmpMsl, sizeof(tmpMsl), "/tmp/tgfx_shader_%lx.metal", (unsigned long)addr);
  snprintf(tmpAir, sizeof(tmpAir), "/tmp/tgfx_shader_%lx.air", (unsigned long)addr);
  snprintf(tmpLib, sizeof(tmpLib), "/tmp/tgfx_shader_%lx.metallib", (unsigned long)addr);

  // Write MSL source to temp file.
  {
    std::ofstream f(tmpMsl, std::ios::binary);
    if (!f.is_open()) {
      return {};
    }
    f.write(mslSource.data(), static_cast<std::streamsize>(mslSource.size()));
  }

  // Compile MSL to AIR.
  char cmd[512];
  const char* stageFlag = (stage == ShaderStageType::Vertex) ? "vertex" : "fragment";
  snprintf(cmd, sizeof(cmd),
           "xcrun -sdk macosx metal -std=macos-metal2.3 -O2 -c %s -o %s 2>/dev/null", tmpMsl,
           tmpAir);
  (void)stageFlag;
  int ret = std::system(cmd);
  std::remove(tmpMsl);
  if (ret != 0) {
    std::remove(tmpAir);
    return {};
  }

  // Link AIR to metallib.
  snprintf(cmd, sizeof(cmd), "xcrun -sdk macosx metallib %s -o %s 2>/dev/null", tmpAir, tmpLib);
  ret = std::system(cmd);
  std::remove(tmpAir);
  if (ret != 0) {
    std::remove(tmpLib);
    return {};
  }

  // Read metallib binary.
  std::ifstream libFile(tmpLib, std::ios::binary | std::ios::ate);
  if (!libFile.is_open()) {
    std::remove(tmpLib);
    return {};
  }
  auto size = libFile.tellg();
  libFile.seekg(0);
  std::vector<uint8_t> data(static_cast<size_t>(size));
  libFile.read(reinterpret_cast<char*>(data.data()), size);
  libFile.close();
  std::remove(tmpLib);
  return data;
}

}  // namespace tgfx
