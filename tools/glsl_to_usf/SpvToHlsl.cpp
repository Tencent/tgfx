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

#include "SpvToHlsl.h"

// Suppress warnings from SPIRV-Cross headers.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Weffc++"
#include <spirv_hlsl.hpp>
#include <spirv_parser.hpp>
#pragma clang diagnostic pop

#include <algorithm>

namespace tgfx {
namespace {

spv::ExecutionModel StageToExecutionModel(GlslStage stage) {
  return stage == GlslStage::Vertex ? spv::ExecutionModelVertex
                                    : spv::ExecutionModelFragment;
}

std::string StageName(GlslStage stage) {
  return stage == GlslStage::Vertex ? "vertex" : "fragment";
}

}  // namespace

SpvToHlslResult ConvertSpvToHlsl(const std::vector<uint32_t>& spirv, GlslStage stage,
                                 uint32_t vertexAttributeCount) {
  SpvToHlslResult result;
  if (spirv.empty()) {
    result.errorMessage = "empty SPIR-V input";
    return result;
  }

  spirv_cross::Parser parser(spirv.data(), spirv.size());
  try {
    parser.parse();
  } catch (...) {
    result.errorMessage = "spirv_cross::Parser::parse() threw";
    return result;
  }

  spirv_cross::CompilerHLSL compiler(std::move(parser.get_parsed_ir()));

  // Use SM 5.1 — the most common shader model UE supports for both D3D11 and D3D12 paths.
  spirv_cross::CompilerHLSL::Options hlslOpts;
  hlslOpts.shader_model = 51;
  hlslOpts.point_size_compat = true;
  hlslOpts.point_coord_compat = true;
  // By default SPIRV-Cross emits the HLSL entry as `main()` regardless of the SPIR-V entry name.
  // Enabling use_entry_point_name makes it honour rename_entry_point / set_entry_point, which we
  // rely on to produce MainVS / MainPS so UE's IMPLEMENT_GLOBAL_SHADER can find the entry.
  hlslOpts.use_entry_point_name = true;
  compiler.set_hlsl_options(hlslOpts);

  auto entryName = stage == GlslStage::Vertex ? std::string("MainVS") : std::string("MainPS");
  compiler.rename_entry_point("main", entryName, StageToExecutionModel(stage));
  compiler.set_entry_point(entryName, StageToExecutionModel(stage));

  // Map SPIR-V input locations to ATTRIBUTEN semantics. UE's input layout maps ATTRIBUTEN to
  // vertex stream slot N, so this conveniently matches the attribute order TGFX already reports.
  if (stage == GlslStage::Vertex) {
    for (uint32_t i = 0; i < vertexAttributeCount; ++i) {
      spirv_cross::HLSLVertexAttributeRemap remap{};
      remap.location = i;
      remap.semantic = "ATTRIBUTE" + std::to_string(i);
      compiler.add_vertex_attribute_remap(remap);
      result.attributes.push_back({i, remap.semantic});
    }
  }

  // Walk SPIR-V resources. UBOs get b-registers in order, sampled images get paired t/s
  // registers. SPIRV-Cross picks up our register declarations via add_hlsl_resource_binding
  // so the final HLSL contains `register(bN)` / `register(tN)` / `register(sN)`.
  auto resources = compiler.get_shader_resources();
  uint32_t cbvIndex = 0;
  uint32_t srvIndex = 0;
  for (auto& ubo : resources.uniform_buffers) {
    auto descSet = compiler.get_decoration(ubo.id, spv::DecorationDescriptorSet);
    auto binding = compiler.get_decoration(ubo.id, spv::DecorationBinding);
    spirv_cross::HLSLResourceBinding b{};
    b.stage = StageToExecutionModel(stage);
    b.desc_set = descSet;
    b.binding = binding;
    b.cbv.register_space = 0;
    b.cbv.register_binding = cbvIndex;
    compiler.add_hlsl_resource_binding(b);
    result.cbvBindings.push_back({ubo.name, StageName(stage), "b", cbvIndex});
    ++cbvIndex;
  }
  for (auto& img : resources.sampled_images) {
    auto descSet = compiler.get_decoration(img.id, spv::DecorationDescriptorSet);
    auto binding = compiler.get_decoration(img.id, spv::DecorationBinding);
    spirv_cross::HLSLResourceBinding b{};
    b.stage = StageToExecutionModel(stage);
    b.desc_set = descSet;
    b.binding = binding;
    b.srv.register_space = 0;
    b.srv.register_binding = srvIndex;
    b.sampler.register_space = 0;
    b.sampler.register_binding = srvIndex;
    compiler.add_hlsl_resource_binding(b);
    result.srvBindings.push_back({img.name, StageName(stage), "t", srvIndex});
    result.samplerBindings.push_back({img.name, StageName(stage), "s", srvIndex});
    ++srvIndex;
  }

  try {
    result.hlsl = compiler.compile();
  } catch (const std::exception& e) {
    result.errorMessage = std::string("spirv_cross compile: ") + e.what();
    return result;
  } catch (...) {
    result.errorMessage = "spirv_cross::CompilerHLSL::compile() threw";
    return result;
  }
  if (result.hlsl.empty()) {
    result.errorMessage = "spirv_cross::CompilerHLSL::compile() returned empty";
  }
  return result;
}

}  // namespace tgfx
