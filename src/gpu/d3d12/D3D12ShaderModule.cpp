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
#include <map>
#include <shaderc/shaderc.hpp>
#include "D3D12GPU.h"
#include "core/utils/Log.h"
#include "gpu/ShaderCompiler.h"
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
//   - UBOs are walked in the order SPIRV-Cross returns them — which is GLSL declaration order —
//     and assigned consecutive CBV registers b0, b1, ... within this single stage. HLSL register
//     namespaces are per-stage so the vertex stage and the pixel stage have independent b0
//     packings; D3D12RenderPipeline's root signature mirrors this by resolving each uniform block
//     by name against the stage's uniformSlots() and giving it a stage-local register index.
//   - Sampled images are walked in the order SPIRV-Cross returns them — which is GLSL
//     declaration order, and therefore matches the BindingLayout::textureSamplers order seen
//     by the pipeline side — and assigned consecutive (t{K}, s{K}) register pairs. The value
//     of the SPIR-V binding decoration is intentionally ignored: it depends on how the GLSL
//     front-end numbers samplers (currently starting from 0 in descriptor set 2), which is a
//     detail of the SPIR-V producer, whereas root-signature construction just needs a dense
//     zero-based register space.
static std::string convertSPIRVToHLSL(const std::vector<uint32_t>& spirvBinary, ShaderStage stage,
                                      const std::vector<ShaderUniformBinding>& declaredUniforms,
                                      std::unordered_map<std::string, unsigned>* uniformRegisters) {
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
  std::map<std::pair<uint32_t, uint32_t>, std::string> uniformNames;
  for (const auto& uniform : declaredUniforms) {
    uniformNames[{uniform.descriptorSet, uniform.binding}] = uniform.name;
  }
  uint32_t cbvRegister = 0;
  for (auto& ubo : resources.uniform_buffers) {
    uint32_t spvBinding = hlslCompiler.get_decoration(ubo.id, spv::DecorationBinding);
    uint32_t spvDescSet = hlslCompiler.get_decoration(ubo.id, spv::DecorationDescriptorSet);
    auto key = std::make_pair(spvDescSet, spvBinding);
    auto name = uniformNames.find(key);
    if (name != uniformNames.end() && uniformRegisters != nullptr) {
      (*uniformRegisters)[name->second] = cbvRegister;
    }
    spirv_cross::HLSLResourceBinding resourceBinding = {};
    resourceBinding.stage = executionModel;
    resourceBinding.desc_set = spvDescSet;
    resourceBinding.binding = spvBinding;
    resourceBinding.cbv.register_binding = cbvRegister++;
    resourceBinding.cbv.register_space = 0;
    hlslCompiler.add_hlsl_resource_binding(resourceBinding);
  }

  // Map combined samplers: assign each SPIR-V sampled image an HLSL (t{K}, s{K}) register pair
  // whose index equals the image's SPIR-V binding decoration. GLSL binding numbers are laid down
  // by ShaderCompiler::assignSamplerBindings in declaration order (0, 1, 2, ...), which is the
  // same order pipeline-side D3D12RenderPipeline::createRootSignature uses to assign SRV/Sampler
  // root parameters. Using spvBinding directly (rather than a separate srvRegister++ counter
  // driven by SPIRV-Cross iteration order) keeps the two sides aligned: get_shader_resources()
  // returns sampled_images in an order that is not guaranteed to match SPIR-V binding order —
  // observed in the GlassStyle AlphaMask path where 3–4 samplers in one FP graph come back with
  // iter/binding pairs like (0, 1), (1, 2), (2, 0), (3, 3), which would silently bind wrong
  // textures at t{K}.
  for (auto& image : resources.sampled_images) {
    uint32_t spvBinding = hlslCompiler.get_decoration(image.id, spv::DecorationBinding);
    uint32_t spvDescSet = hlslCompiler.get_decoration(image.id, spv::DecorationDescriptorSet);
    spirv_cross::HLSLResourceBinding resourceBinding = {};
    resourceBinding.stage = executionModel;
    resourceBinding.desc_set = spvDescSet;
    resourceBinding.binding = spvBinding;
    resourceBinding.srv.register_binding = spvBinding;
    resourceBinding.srv.register_space = 0;
    resourceBinding.sampler.register_binding = spvBinding;
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
  // Use the same optimization level in Debug and Release so shader output is bit-stable across
  // build configurations. Skipping optimization in Debug changes floating-point evaluation order
  // (loop unrolling, FMA fusion, instruction reordering), which propagates through packed-float
  // encoded intermediate textures (see TentBlur1DFragmentProcessor) and is then amplified by
  // finite-difference gradient + normalize + distance scaling in the UDF refraction path,
  // yielding visibly different results between Debug and Release builds.
  UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
  flags |= D3DCOMPILE_DEBUG;
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
    : VaryingShaderModule(ExtractVaryingDecls(descriptor.code, descriptor.stage), {}),
      _stage(descriptor.stage) {
  std::string vulkanGLSL = PreprocessGLSL(descriptor.code, descriptor.stage);
  auto declaredUniforms = detail::CollectUniformBindings(vulkanGLSL);
  // D3D12 needs every declared interface variable to survive — see ShaderCompiler.h.
  auto spirvBinary = CompileGLSLToSPIRV(gpu->shaderCompiler(), vulkanGLSL, descriptor.stage, true);
  if (spirvBinary.empty()) {
    LOGE("D3D12ShaderModule: GLSL to SPIR-V compilation failed.");
    return;
  }
  // SPIRV-Cross assigns the CBV register (b{N}) each uniform block occupies; capture that as the
  // per-stage physical slot and publish it to the base class for pipeline-side resolution.
  std::unordered_map<std::string, unsigned> uniformRegisters;
  std::string hlsl =
      convertSPIRVToHLSL(spirvBinary, descriptor.stage, declaredUniforms, &uniformRegisters);
  if (hlsl.empty()) {
    return;
  }
  setUniformSlots(
      std::map<std::string, unsigned>(uniformRegisters.begin(), uniformRegisters.end()));
  bytecode = compileHLSLToDXBC(hlsl, descriptor.stage);
#ifdef TGFX_D3D12_DEBUG_LAYER
  _hlslSource = std::move(hlsl);
#endif
}

void D3D12ShaderModule::onRelease(D3D12GPU*) {
  // ID3DBlob is reference counted via ComPtr; releasing the ComPtr frees the bytecode.
  bytecode = nullptr;
}

}  // namespace tgfx
