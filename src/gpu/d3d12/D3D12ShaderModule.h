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
#include <unordered_map>
#include "D3D12Resource.h"
#include "D3D12Util.h"
#include "gpu/VaryingShaderModule.h"
#include "tgfx/gpu/ShaderModule.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {

class D3D12GPU;

/**
 * D3D12 shader module implementation.
 *
 * The backend consumes HLSL source compiled at runtime by the system D3DCompiler (D3DCompile,
 * profiles vs_5_0 / ps_5_0), or pre-compiled DXBC bytecode loaded verbatim. No third-party
 * compiler is involved. The resulting DXBC blob is consumed by D3D12RenderPipeline via
 * shaderBytecode().
 *
 * HLSL contract the module expects (documented on ShaderCodeFormat::HLSL):
 *   - Entry function name: `main`.
 *   - CBVs: a uniform block binds the register(bN) its HLSL declares; the module reports N
 *     through uniformSlots so the root signature can use the same value. Registers are
 *     per-stage: VS b0 and FS b0 do not collide because the root signature distinguishes them
 *     via ShaderVisibility.
 *   - SRVs/Samplers: texture samplers bind at paired (t{K}, s{K}) registers, where K is the
 *     entry's index in BindingLayout::textureSamplers.
 *   - Vertex inputs use the TEXCOORD{N} semantic, where N is the attribute's global index across
 *     the pipeline vertex buffer layouts. D3D12RenderPipeline builds the input layout from that
 *     index, so the semantics must stay in lock step with it.
 * The mapping is resolved by name against ShaderModuleDescriptor::uniformSlots, so the module
 * and the pipeline layout must agree on every block name.
 */
class D3D12ShaderModule : public VaryingShaderModule, public D3D12Resource {
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
