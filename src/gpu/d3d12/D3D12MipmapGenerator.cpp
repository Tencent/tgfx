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

#include "D3D12MipmapGenerator.h"
#include <d3dcompiler.h>
#include "D3D12GPU.h"
#include "core/utils/Log.h"

namespace tgfx {

// Box-filter compute shader: each thread samples 2x2 source texels and writes one destination
// texel. SamplePoint is required because textures we generate mipmaps on may not have linear
// sampling enabled by the time this shader runs; sampling four corners with bilinear would
// average twice and double the cost. The HLSL is intentionally inline and tiny so D3DCompile
// finishes in well under a millisecond.
//
// Layout matches the root signature in createRootSignature():
//   register(b0) — uint4 with mip dimensions and 1/dimensions
//   register(t0) — input mip (mip[i])
//   register(s0) — point sampler with clamp address mode
//   register(u0) — output mip (mip[i+1])
static constexpr const char* kHLSLSource = R"(
cbuffer MipmapCB : register(b0)
{
    uint  OutMipWidth;
    uint  OutMipHeight;
    float InvOutMipWidth;
    float InvOutMipHeight;
};

Texture2D<float4>   InputMip  : register(t0);
SamplerState        PointClamp : register(s0);
RWTexture2D<float4> OutputMip : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtID : SV_DispatchThreadID)
{
    if (dtID.x >= OutMipWidth || dtID.y >= OutMipHeight) {
        return;
    }
    // Sample at the four sub-pixel corners of the destination texel.
    float2 uv = (float2(dtID.xy) + 0.5f) * float2(InvOutMipWidth, InvOutMipHeight);
    float4 c0 = InputMip.SampleLevel(PointClamp, uv + float2(-0.25f * InvOutMipWidth, -0.25f * InvOutMipHeight), 0);
    float4 c1 = InputMip.SampleLevel(PointClamp, uv + float2( 0.25f * InvOutMipWidth, -0.25f * InvOutMipHeight), 0);
    float4 c2 = InputMip.SampleLevel(PointClamp, uv + float2(-0.25f * InvOutMipWidth,  0.25f * InvOutMipHeight), 0);
    float4 c3 = InputMip.SampleLevel(PointClamp, uv + float2( 0.25f * InvOutMipWidth,  0.25f * InvOutMipHeight), 0);
    OutputMip[dtID.xy] = 0.25f * (c0 + c1 + c2 + c3);
}
)";

D3D12MipmapGenerator::D3D12MipmapGenerator(D3D12GPU* gpu) {
  if (!createRootSignature(gpu)) {
    return;
  }
  if (!createPipelineState(gpu)) {
    _rootSignature = nullptr;
  }
}

D3D12MipmapGenerator* D3D12MipmapGenerator::Get(D3D12GPU* gpu) {
  return gpu->mipmapGenerator();
}

bool D3D12MipmapGenerator::createRootSignature(D3D12GPU* gpu) {
  // Constants + SRV table + UAV table. A single static sampler (point/clamp) means we don't have
  // to thread a sampler descriptor heap through generateMipmapsForTexture().
  D3D12_DESCRIPTOR_RANGE srvRange = {};
  srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srvRange.NumDescriptors = 1;
  srvRange.BaseShaderRegister = 0;
  srvRange.RegisterSpace = 0;
  srvRange.OffsetInDescriptorsFromTableStart = 0;

  D3D12_DESCRIPTOR_RANGE uavRange = {};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = 1;
  uavRange.BaseShaderRegister = 0;
  uavRange.RegisterSpace = 0;
  uavRange.OffsetInDescriptorsFromTableStart = 0;

  D3D12_ROOT_PARAMETER params[3] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[0].Constants.ShaderRegister = 0;
  params[0].Constants.RegisterSpace = 0;
  params[0].Constants.Num32BitValues = 4;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &srvRange;

  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[2].DescriptorTable.NumDescriptorRanges = 1;
  params[2].DescriptorTable.pDescriptorRanges = &uavRange;

  D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
  samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samplerDesc.MipLODBias = 0.0f;
  samplerDesc.MaxAnisotropy = 1;
  samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
  samplerDesc.MinLOD = 0.0f;
  samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
  samplerDesc.ShaderRegister = 0;
  samplerDesc.RegisterSpace = 0;
  samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
  rootSigDesc.NumParameters = 3;
  rootSigDesc.pParameters = params;
  rootSigDesc.NumStaticSamplers = 1;
  rootSigDesc.pStaticSamplers = &samplerDesc;
  rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> blob = nullptr;
  ComPtr<ID3DBlob> errorBlob = nullptr;
  auto hr =
      D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errorBlob);
  if (FAILED(hr)) {
    LOGE("D3D12MipmapGenerator: D3D12SerializeRootSignature failed (HRESULT=0x%08X).",
         static_cast<unsigned>(hr));
    return false;
  }
  hr = gpu->device()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                          IID_PPV_ARGS(&_rootSignature));
  if (FAILED(hr)) {
    LOGE("D3D12MipmapGenerator: CreateRootSignature failed (HRESULT=0x%08X).",
         static_cast<unsigned>(hr));
    return false;
  }
  return true;
}

bool D3D12MipmapGenerator::createPipelineState(D3D12GPU* gpu) {
  ComPtr<ID3DBlob> csBlob = nullptr;
  ComPtr<ID3DBlob> errorBlob = nullptr;
  UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
  compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
  auto hr = D3DCompile(kHLSLSource, strlen(kHLSLSource), nullptr, nullptr, nullptr, "main",
                       "cs_5_0", compileFlags, 0, &csBlob, &errorBlob);
  if (FAILED(hr)) {
    LOGE("D3D12MipmapGenerator: D3DCompile failed (HRESULT=0x%08X): %s",
         static_cast<unsigned>(hr),
         errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "<no message>");
    return false;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = _rootSignature.Get();
  desc.CS.pShaderBytecode = csBlob->GetBufferPointer();
  desc.CS.BytecodeLength = csBlob->GetBufferSize();
  desc.NodeMask = 0;
  desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

  hr = gpu->device()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&_pipelineState));
  if (FAILED(hr)) {
    LOGE("D3D12MipmapGenerator: CreateComputePipelineState failed (HRESULT=0x%08X).",
         static_cast<unsigned>(hr));
    return false;
  }
  return true;
}

}  // namespace tgfx
