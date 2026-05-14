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
 * Resource binding mapping (SPIR-V binding -> HLSL register):
 *   - VertexUniformBlock   (binding 0) -> b0
 *   - FragmentUniformBlock (binding 1) -> b0  (HLSL b/t/s registers are per shader stage; both
 *                                              stages can use b0 without colliding because the
 *                                              D3D12 root signature distinguishes them via
 *                                              ShaderVisibility.)
 *   - sampler bindings (binding N >= 2) -> t{N-2} + s{N-2}
 *
 * SPIRV-Cross's default behaviour already matches CBV/SRV/Sampler register classes derived from
 * the SPIR-V resource type, so the only customisation we need is shifting samplers to register 0.
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

 protected:
  void onRelease(D3D12GPU* gpu) override;

 private:
  D3D12ShaderModule(D3D12GPU* gpu, const ShaderModuleDescriptor& descriptor);
  ~D3D12ShaderModule() override = default;

  ShaderStage _stage = ShaderStage::Vertex;
  ComPtr<ID3DBlob> bytecode = nullptr;

  friend class D3D12GPU;
};

}  // namespace tgfx
