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
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <shaderc/shaderc.hpp>
#include <spirv_msl.hpp>
#include <spirv_parser.hpp>

namespace tgfx {

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
                          const std::string& shaderName, uint32_t variantIndex, bool optimize) {
  CompileResult result;
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(optimize ? shaderc_optimization_level_performance
                                        : shaderc_optimization_level_zero);
  options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);

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

CompileResult TranslateToWGSL(const std::vector<uint32_t>& /*spirv*/) {
  CompileResult result;
  // Tint integration requires complex build setup. Placeholder for now - will be completed
  // when the tint build dependency is properly configured for the standalone tool.
  result.success = false;
  result.error = "WGSL translation not yet implemented in build tool.";
  return result;
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
