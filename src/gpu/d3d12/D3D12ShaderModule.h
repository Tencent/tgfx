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

#pragma once

#include <d3dcompiler.h>
#include <string>
#include "D3D12Resource.h"
#include "D3D12Util.h"
#include "tgfx/gpu/ShaderModule.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {

class D3D12GPU;

/**
 * D3D12 shader module implementation.
 *
 * Compilation pipeline (matching the GLSL-first design used by Vulkan and Metal backends):
 *   1. PreprocessGLSL — assigns explicit binding/location qualifiers (shared with Vulkan/Metal).
 *   2. CompileGLSLToSPIRV — uses shaderc to produce SPIR-V (shared with Vulkan/Metal).
 *   3. SPIR-V -> HLSL — uses spirv_cross::CompilerHLSL targeting shader model 5.0.
 *   4. HLSL -> DXBC — uses D3DCompile with profile vs_5_0 / ps_5_0.
 *
 * The resulting DXBC blob is consumed by D3D12RenderPipeline via shaderBytecode().
 *
 * HLSL register assignment:
 *   - CBVs: each uniform_buffer entry SPIRV-Cross reports for this stage receives the next
 *     free b{N} register (b0, b1, ...), assigned in declaration order (SPIRV-Cross iterates
 *     resources by SPIR-V id, which matches the order they appear in the GLSL source).
 *     Registers are per-stage: VS b0 and FS b0 do not collide because the D3D12 root signature
 *     distinguishes them via ShaderVisibility.
 *   - SRVs/Samplers: each sampled_image entry receives a paired (t{K}, s{K}) register slot
 *     assigned in the same declaration order, again starting at 0.
 *   - The SPIR-V binding number attached to the source declaration does not influence the HLSL
 *     register number — SPIRV-Cross's default binding-derived mapping is fully overridden here
 *     via add_hlsl_resource_binding. This dense ordering is intentional: it must stay in lock
 *     step with the order D3D12RenderPipeline::createRootSignature walks
 *     BindingLayout::uniformBlocks and BindingLayout::textureSamplers, so the shader's b/t/s
 *     slots correspond one-to-one with the root parameter tables. Reordering either loop, or
 *     changing SPIRV-Cross's resource iteration order, will silently mis-map bindings.
 */
class D3D12ShaderModule : public ShaderModule, public D3D12Resource {
 public:
  static std::shared_ptr<D3D12ShaderModule> Make(D3D12GPU* gpu,
                                                 const ShaderModuleDescriptor& descriptor);

  /**
   * Returns the compiled DXBC bytecode in the form expected by D3D12 pipeline state descriptors.
   * The returned struct references memory owned by this object; its lifetime is bound to the
   * lifetime of the D3D12ShaderModule.
   */
  D3D12_SHADER_BYTECODE shaderBytecode() const {
    if (bytecode == nullptr) {
      return {nullptr, 0};
    }
    return {bytecode->GetBufferPointer(), bytecode->GetBufferSize()};
  }

  ShaderStage stage() const {
    return _stage;
  }

#ifdef TGFX_D3D12_DEBUG_LAYER
  /// Returns the cross-compiled HLSL source captured during construction. Diagnostic-only:
  /// available only when TGFX_D3D12_DEBUG_LAYER is defined so production builds don't pay the
  /// memory cost of holding HLSL strings.
  const std::string& hlslSource() const {
    return _hlslSource;
  }
#endif

 protected:
  void onRelease(D3D12GPU* gpu) override;

 private:
  D3D12ShaderModule(D3D12GPU* gpu, const ShaderModuleDescriptor& descriptor);
  ~D3D12ShaderModule() override = default;

  ShaderStage _stage = ShaderStage::Vertex;
  ComPtr<ID3DBlob> bytecode = nullptr;
#ifdef TGFX_D3D12_DEBUG_LAYER
  std::string _hlslSource;
#endif

  friend class D3D12GPU;
};

}  // namespace tgfx
