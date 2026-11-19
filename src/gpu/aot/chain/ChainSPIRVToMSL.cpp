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
//  either express or implied. See the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/chain/ChainSPIRVToMSL.h"

// The translation exists only where Metal does: SPIRV-Cross is linked into the runtime on that
// configuration alone. Other builds get the declining stub below.
#if defined(TGFX_USE_METAL)

#include <exception>
#include <spirv_cross.hpp>
#include <spirv_msl.hpp>
#include <spirv_parser.hpp>

namespace tgfx {

bool TranslateChainSPIRVToMSL(const std::vector<uint32_t>& words, std::string* msl,
                              std::string* error) {
  if (msl == nullptr || error == nullptr) {
    return false;
  }
  // spirv-cross reports through C++ exceptions; the boundary converts them into a return value
  // (the project forbids exceptions in its own code).
  try {
    spirv_cross::Parser parser(words);
    parser.parse();
    spirv_cross::CompilerMSL compiler(std::move(parser.get_parsed_ir()));

    spirv_cross::CompilerMSL::Options options;
    options.set_msl_version(2, 3);
    options.enable_decoration_binding = true;
    compiler.set_msl_options(options);

    auto commonOptions = compiler.get_common_options();
    commonOptions.vertex.flip_vert_y = true;
    compiler.set_common_options(commonOptions);

    // Binding numbers map straight onto the MSL index spaces — the same convention the build
    // tool's translation and the runtime's metal descriptors use.
    auto resources = compiler.get_shader_resources();
    for (auto& ubo : resources.uniform_buffers) {
      spirv_cross::MSLResourceBinding binding = {};
      binding.stage = spv::ExecutionModelFragment;
      binding.desc_set = compiler.get_decoration(ubo.id, spv::DecorationDescriptorSet);
      binding.binding = compiler.get_decoration(ubo.id, spv::DecorationBinding);
      binding.msl_buffer = binding.binding;
      compiler.add_msl_resource_binding(binding);
    }
    for (auto& image : resources.sampled_images) {
      spirv_cross::MSLResourceBinding binding = {};
      binding.stage = spv::ExecutionModelFragment;
      binding.desc_set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
      binding.binding = compiler.get_decoration(image.id, spv::DecorationBinding);
      binding.msl_texture = binding.binding;
      binding.msl_sampler = binding.binding;
      compiler.add_msl_resource_binding(binding);
    }

    *msl = compiler.compile();
    return !msl->empty();
  } catch (const spirv_cross::CompilerError& exception) {
    *error = std::string("spirv-cross MSL error: ") + exception.what();
  } catch (const std::exception& exception) {
    *error = std::string("spirv-cross exception: ") + exception.what();
  }
  return false;
}

}  // namespace tgfx

#else  // TGFX_USE_METAL

namespace tgfx {

bool TranslateChainSPIRVToMSL(const std::vector<uint32_t>&, std::string*, std::string* error) {
  if (error != nullptr) {
    *error = "the Metal translation route is not part of this build";
  }
  return false;
}

}  // namespace tgfx

#endif  // TGFX_USE_METAL
