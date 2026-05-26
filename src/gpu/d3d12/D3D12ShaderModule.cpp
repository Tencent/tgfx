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

#include "D3D12ShaderModule.h"
#include <shaderc/shaderc.hpp>
#include "D3D12GPU.h"
#include "core/utils/Log.h"
#include "gpu/ShaderCompiler.h"
#include "gpu/UniformData.h"
#ifdef TGFX_D3D12_PERF_TRACE
#include "tgfx/core/Clock.h"
#endif
// Suppress warnings from SPIRV-Cross headers
#pragma warning(push)
#pragma warning(disable : 4100 4458 4245 4127 4244)
#include <spirv_hlsl.hpp>
#include <spirv_parser.hpp>
#pragma warning(pop)

namespace tgfx {

// Convert a SPIR-V binary to HLSL source code suitable for D3DCompile with profile vs_5_0/ps_5_0.
//
// Binding strategy:
//   - UBOs are walked in the order SPIRV-Cross returns them — which is GLSL declaration order,
//     and therefore matches the BindingLayout::uniformBlocks order seen by the pipeline side —
//     and assigned consecutive CBV registers b0, b1, ... within this single stage. HLSL register
//     namespaces are per-stage so the vertex stage and the pixel stage have independent b0
//     packings; D3D12RenderPipeline's root signature mirrors this by giving each entry a
//     stage-local register index.
//   - Sampled images at SPIR-V bindings 2..N are mapped to (t{N-2}, s{N-2}). Shifting by
//     TEXTURE_BINDING_POINT_START keeps the t/s register space dense starting at zero, which
//     simplifies root-signature construction.
static std::string convertSPIRVToHLSL(const std::vector<uint32_t>& spirvBinary, ShaderStage stage) {
  spirv_cross::Parser spvParser(spirvBinary.data(), spirvBinary.size());
  spvParser.parse();
  spirv_cross::CompilerHLSL hlslCompiler(std::move(spvParser.get_parsed_ir()));

  spirv_cross::CompilerHLSL::Options hlslOptions;
  hlslOptions.shader_model = 50;
  hlslCompiler.set_hlsl_options(hlslOptions);

  auto commonOptions = hlslCompiler.get_common_options();
  // Compensate for HLSL's clip-space Y direction matching Vulkan/GL after our standard flip.
  commonOptions.vertex.flip_vert_y = true;
  hlslCompiler.set_common_options(commonOptions);

  auto executionModel =
      (stage == ShaderStage::Vertex) ? spv::ExecutionModelVertex : spv::ExecutionModelFragment;

  auto resources = hlslCompiler.get_shader_resources();

  // Map UBOs: for the current stage, walk the SPIR-V uniform buffers in the order produced by
  // SPIRV-Cross (which matches GLSL declaration order, identical to BindingLayout's
  // uniformBlocks order on the pipeline side) and assign them HLSL CBV registers b0, b1, ...
  // sequentially. HLSL register namespaces are per-stage, so this gives each stage a dense
  // packing that matches D3D12RenderPipeline::createRootSignature, which assigns the same
  // stage-local index to each entry's CBV root parameter.
  uint32_t cbvRegister = 0;
  for (auto& ubo : resources.uniform_buffers) {
    uint32_t spvBinding = hlslCompiler.get_decoration(ubo.id, spv::DecorationBinding);
    uint32_t spvDescSet = hlslCompiler.get_decoration(ubo.id, spv::DecorationDescriptorSet);
    spirv_cross::HLSLResourceBinding resourceBinding = {};
    resourceBinding.stage = executionModel;
    resourceBinding.desc_set = spvDescSet;
    resourceBinding.binding = spvBinding;
    resourceBinding.cbv.register_binding = cbvRegister++;
    resourceBinding.cbv.register_space = 0;
    hlslCompiler.add_hlsl_resource_binding(resourceBinding);
  }

  // Map combined samplers: SPIR-V binding N -> (t{N - TEXTURE_BINDING_POINT_START},
  // s{N - TEXTURE_BINDING_POINT_START}).
  for (auto& image : resources.sampled_images) {
    uint32_t spvBinding = hlslCompiler.get_decoration(image.id, spv::DecorationBinding);
    uint32_t spvDescSet = hlslCompiler.get_decoration(image.id, spv::DecorationDescriptorSet);
    uint32_t hlslSlot = (spvBinding >= static_cast<uint32_t>(TEXTURE_BINDING_POINT_START))
                            ? spvBinding - static_cast<uint32_t>(TEXTURE_BINDING_POINT_START)
                            : spvBinding;
    spirv_cross::HLSLResourceBinding resourceBinding = {};
    resourceBinding.stage = executionModel;
    resourceBinding.desc_set = spvDescSet;
    resourceBinding.binding = spvBinding;
    resourceBinding.srv.register_binding = hlslSlot;
    resourceBinding.srv.register_space = 0;
    resourceBinding.sampler.register_binding = hlslSlot;
    resourceBinding.sampler.register_space = 0;
    hlslCompiler.add_hlsl_resource_binding(resourceBinding);
  }

  std::string hlsl = hlslCompiler.compile();
  if (hlsl.empty()) {
    LOGE("D3D12ShaderModule: SPIR-V to HLSL conversion produced empty source.");
  }
  return hlsl;
}

// Compile HLSL source to a DXBC bytecode blob using D3DCompile with the appropriate stage profile.
static ComPtr<ID3DBlob> compileHLSLToDXBC(const std::string& hlsl, ShaderStage stage) {
  const char* target = (stage == ShaderStage::Vertex) ? "vs_5_0" : "ps_5_0";
  UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
  flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

  ComPtr<ID3DBlob> codeBlob = nullptr;
  ComPtr<ID3DBlob> errorBlob = nullptr;
  auto hr = D3DCompile(hlsl.data(), hlsl.size(), nullptr, nullptr, nullptr, "main", target, flags,
                       0, &codeBlob, &errorBlob);
  if (FAILED(hr)) {
    if (errorBlob != nullptr) {
      LOGE("D3D12ShaderModule: D3DCompile failed (HRESULT=0x%08X): %s", static_cast<unsigned>(hr),
           static_cast<const char*>(errorBlob->GetBufferPointer()));
    } else {
      LOGE("D3D12ShaderModule: D3DCompile failed (HRESULT=0x%08X) with no error message.",
           static_cast<unsigned>(hr));
    }
    LOGE("D3D12ShaderModule: HLSL source (first 1024 chars):\n%.1024s", hlsl.c_str());
    return nullptr;
  }
  return codeBlob;
}

std::shared_ptr<D3D12ShaderModule> D3D12ShaderModule::Make(
    D3D12GPU* gpu, const ShaderModuleDescriptor& descriptor) {
  if (gpu == nullptr) {
    return nullptr;
  }
  auto module = gpu->makeResource<D3D12ShaderModule>(gpu, descriptor);
  if (module->bytecode == nullptr) {
    return nullptr;
  }
  return module;
}

D3D12ShaderModule::D3D12ShaderModule(D3D12GPU* gpu, const ShaderModuleDescriptor& descriptor)
    : _stage(descriptor.stage) {
#ifdef TGFX_D3D12_PERF_TRACE
  auto t0 = Clock::Now();
#endif
  std::string vulkanGLSL = PreprocessGLSL(descriptor.code);
  // D3D12 needs every declared interface variable to survive — see ShaderCompiler.h.
  auto spirvBinary = CompileGLSLToSPIRV(gpu->shaderCompiler(), vulkanGLSL, descriptor.stage, true);
  if (spirvBinary.empty()) {
    LOGE("D3D12ShaderModule: GLSL to SPIR-V compilation failed.");
    return;
  }
#ifdef TGFX_D3D12_PERF_TRACE
  auto t1 = Clock::Now();
#endif
  std::string hlsl = convertSPIRVToHLSL(spirvBinary, descriptor.stage);
  if (hlsl.empty()) {
    return;
  }
#ifdef TGFX_D3D12_PERF_TRACE
  auto t2 = Clock::Now();
#endif
  bytecode = compileHLSLToDXBC(hlsl, descriptor.stage);
#ifdef TGFX_D3D12_PERF_TRACE
  auto t3 = Clock::Now();
  const auto* stageName = (descriptor.stage == ShaderStage::Vertex) ? "VS" : "FS";
  LOGI(
      "[D3D12-Perf] ShaderModule(%s) glsl=%lluus spv2hlsl=%lluus hlsl2dxbc=%lluus total=%lluus "
      "src=%zuB dxbc=%zuB",
      stageName, static_cast<unsigned long long>(t1 - t0), static_cast<unsigned long long>(t2 - t1),
      static_cast<unsigned long long>(t3 - t2), static_cast<unsigned long long>(t3 - t0),
      descriptor.code.size(),
      bytecode != nullptr ? static_cast<size_t>(bytecode->GetBufferSize())
                          : static_cast<size_t>(0));
#endif
#ifdef TGFX_D3D12_DEBUG_LAYER
  _hlslSource = std::move(hlsl);
#endif
}

void D3D12ShaderModule::onRelease(D3D12GPU*) {
  // ID3DBlob is reference counted via ComPtr; releasing the ComPtr frees the bytecode.
  bytecode = nullptr;
}

}  // namespace tgfx
