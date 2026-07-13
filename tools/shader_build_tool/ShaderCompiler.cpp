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
                          const std::string& shaderName, uint32_t variantIndex) {
  CompileResult result;
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(shaderc_optimization_level_zero);
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

}  // namespace tgfx
