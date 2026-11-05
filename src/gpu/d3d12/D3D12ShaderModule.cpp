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
#include <d3d12shader.h>
#include <cstring>
#include <utility>
#include "D3D12GPU.h"
#include "core/utils/Log.h"
#include "gpu/ShaderInterface.h"

namespace tgfx {

// Compile HLSL source to a DXBC bytecode blob using D3DCompile with the appropriate stage profile.
static ComPtr<ID3DBlob> compileHLSLToDXBC(const std::string& hlsl, ShaderStage stage) {
  const char* target = (stage == ShaderStage::Vertex) ? "vs_5_0" : "ps_5_0";
  // Use the same optimization level in Debug and Release so shader output is bit-stable across
  // build configurations. Skipping optimization in Debug changes floating-point evaluation order
  // (loop unrolling, FMA fusion, instruction reordering), which propagates through packed-float
  // encoded intermediate textures (see GlassUDFTentBlurFragmentProcessor) and is then amplified
  // by finite-difference gradient + normalize + distance scaling in the UDF refraction path,
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
    : VaryingShaderModule(ExtractModuleVaryingDecls(descriptor), {}), _stage(descriptor.stage) {
  // Unused: the module only needs the device for resource bookkeeping.
  (void)gpu;
  setUniformSlots(descriptor.uniformSlots);
  switch (descriptor.format) {
    case ShaderCodeFormat::HLSL:
      if (descriptor.code.empty()) {
        LOGE("D3D12ShaderModule: HLSL source is empty.");
        return;
      }
      bytecode = compileHLSLToDXBC(descriptor.code, descriptor.stage);
#ifdef TGFX_D3D12_DEBUG_LAYER
      _hlslSource = descriptor.code;
#endif
      break;
    case ShaderCodeFormat::DXBC: {
      if (descriptor.binaryData.empty()) {
        LOGE("D3D12ShaderModule: DXBC bytecode is empty.");
        return;
      }
      ComPtr<ID3DBlob> blob = nullptr;
      if (FAILED(D3DCreateBlob(descriptor.binaryData.size(), &blob))) {
        LOGE("D3D12ShaderModule: failed to allocate a DXBC blob.");
        return;
      }
      memcpy(blob->GetBufferPointer(), descriptor.binaryData.data(), descriptor.binaryData.size());
      bytecode = std::move(blob);
      break;
    }
    default:
      LOGE(
          "D3D12ShaderModule: unsupported shader format; the D3D12 backend only accepts HLSL "
          "text or DXBC bytecode.");
      return;
  }
  collectInputSemantics();
}

void D3D12ShaderModule::collectInputSemantics() {
  // Only the vertex stage has an input signature the pipeline's input layout must match.
  if (_stage != ShaderStage::Vertex || bytecode == nullptr) {
    return;
  }
  ComPtr<ID3D12ShaderReflection> reflection = nullptr;
  if (FAILED(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
                        IID_PPV_ARGS(&reflection)))) {
    // Leave _signatureKnown false: hasInputSemantic then reports true for everything, which is
    // the unfiltered behaviour that predates signature filtering.
    LOGE("D3D12ShaderModule: D3DReflect failed; the input layout will not be filtered.");
    return;
  }
  D3D12_SHADER_DESC shaderDesc = {};
  if (FAILED(reflection->GetDesc(&shaderDesc))) {
    LOGE("D3D12ShaderModule: reading the reflection description failed.");
    return;
  }
  for (UINT i = 0; i < shaderDesc.InputParameters; i++) {
    D3D12_SIGNATURE_PARAMETER_DESC param = {};
    if (FAILED(reflection->GetInputParameterDesc(i, &param))) {
      continue;
    }
    // The pipeline emits TEXCOORD semantics only (see D3D12RenderPipeline); a signature entry
    // under another semantic is nothing it could bind, so it is not collected.
    if (std::strcmp(param.SemanticName, "TEXCOORD") == 0) {
      _inputSemantics.insert(param.SemanticIndex);
    }
  }
  _signatureKnown = true;
}

void D3D12ShaderModule::onRelease(D3D12GPU*) {
  // ID3DBlob is reference counted via ComPtr; releasing the ComPtr frees the bytecode.
  bytecode = nullptr;
}

}  // namespace tgfx
