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

#include "D3D12RenderPipeline.h"
#include <d3dcompiler.h>
#include <vector>
#include "D3D12GPU.h"
#include "D3D12ShaderModule.h"
#include "core/utils/Log.h"
#include "gpu/UniformData.h"
#include "tgfx/gpu/ColorWriteMask.h"
#include "tgfx/gpu/ShaderVisibility.h"
#ifdef TGFX_D3D12_PERF_TRACE
#include "tgfx/core/Clock.h"
#endif

namespace tgfx {

// Map a TGFX ShaderVisibility bitmask to the D3D12 single-stage enum used by root parameters.
// D3D12 only allows a single visibility per root parameter; combinations fall back to ALL.
static D3D12_SHADER_VISIBILITY ToD3D12ShaderVisibility(uint32_t visibility) {
  if (visibility == ShaderVisibility::Vertex) {
    return D3D12_SHADER_VISIBILITY_VERTEX;
  }
  if (visibility == ShaderVisibility::Fragment) {
    return D3D12_SHADER_VISIBILITY_PIXEL;
  }
  return D3D12_SHADER_VISIBILITY_ALL;
}

static UINT8 ToD3D12RenderTargetWriteMask(uint32_t mask) {
  UINT8 result = 0;
  if (mask & ColorWriteMask::RED) result |= D3D12_COLOR_WRITE_ENABLE_RED;
  if (mask & ColorWriteMask::GREEN) result |= D3D12_COLOR_WRITE_ENABLE_GREEN;
  if (mask & ColorWriteMask::BLUE) result |= D3D12_COLOR_WRITE_ENABLE_BLUE;
  if (mask & ColorWriteMask::ALPHA) result |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
  return result;
}

// True when the descriptor declares any non-default stencil state, matching the same predicate
// used by the Vulkan backend so that pipeline state is consistent across backends.
static bool HasNonTrivialStencilState(const DepthStencilDescriptor& ds) {
  return ds.stencilFront.compare != CompareFunction::Always ||
         ds.stencilBack.compare != CompareFunction::Always ||
         ds.stencilFront.failOp != StencilOperation::Keep ||
         ds.stencilFront.passOp != StencilOperation::Keep ||
         ds.stencilFront.depthFailOp != StencilOperation::Keep ||
         ds.stencilBack.failOp != StencilOperation::Keep ||
         ds.stencilBack.passOp != StencilOperation::Keep ||
         ds.stencilBack.depthFailOp != StencilOperation::Keep;
}

std::shared_ptr<D3D12RenderPipeline> D3D12RenderPipeline::Make(
    D3D12GPU* gpu, const RenderPipelineDescriptor& descriptor) {
  if (gpu == nullptr) {
    return nullptr;
  }
  auto pipeline = gpu->makeResource<D3D12RenderPipeline>(gpu, descriptor);
  if (pipeline->pipelineState == nullptr) {
    return nullptr;
  }
  return pipeline;
}

D3D12RenderPipeline::D3D12RenderPipeline(D3D12GPU* gpu,
                                         const RenderPipelineDescriptor& descriptor) {
  if (!createRootSignature(gpu, descriptor)) {
    return;
  }
  if (!createPipelineState(gpu, descriptor)) {
    return;
  }
}

void D3D12RenderPipeline::onRelease(D3D12GPU*) {
  pipelineState = nullptr;
  rootSignature = nullptr;
}

uint32_t D3D12RenderPipeline::getUniformRootParameterIndex(unsigned binding) const {
  auto it = uniformRootParameterIndex.find(binding);
  return it != uniformRootParameterIndex.end() ? it->second : UINT32_MAX;
}

uint32_t D3D12RenderPipeline::getTextureRootParameterIndex(unsigned binding) const {
  auto it = textureRootParameterIndex.find(binding);
  return it != textureRootParameterIndex.end() ? it->second : UINT32_MAX;
}

uint32_t D3D12RenderPipeline::getSamplerRootParameterIndex(unsigned binding) const {
  auto it = samplerRootParameterIndex.find(binding);
  return it != samplerRootParameterIndex.end() ? it->second : UINT32_MAX;
}

unsigned D3D12RenderPipeline::getTextureIndex(unsigned binding) const {
  auto it = textureUnits.find(binding);
  return it != textureUnits.end() ? it->second : binding;
}

uint32_t D3D12RenderPipeline::getUniformBlockVisibility(unsigned binding) const {
  auto it = uniformBlockVisibility.find(binding);
  return it != uniformBlockVisibility.end() ? it->second : ShaderVisibility::VertexFragment;
}

bool D3D12RenderPipeline::createRootSignature(D3D12GPU* gpu,
                                              const RenderPipelineDescriptor& descriptor) {
  // First, populate the per-binding index maps — those are needed for every pipeline regardless
  // of whether the underlying ID3D12RootSignature is cached. Walk uniform blocks first, then
  // texture samplers, so the parameter indices line up with the order used when serialising.
  std::vector<uint8_t> shapeKey;
  // Reserve roughly: 1 byte UBO count + 4 bytes per UBO (2 visibility + 1 vertex register +
  // 1 fragment register) + 1 byte sampler count + 2 bytes per sampler (visibility).
  shapeKey.reserve(2 + descriptor.layout.uniformBlocks.size() * 4 +
                   descriptor.layout.textureSamplers.size() * 2);

  // Pre-scan uniform blocks to compute, for every entry, its 0-based register index inside the
  // vertex and fragment stages. SPIR-V binding K is mapped to HLSL register b{idx} where idx is
  // the entry's position among same-stage entries in BindingLayout.uniformBlocks. Two entries
  // visible to the same stage must therefore yield different register indices, and the indices
  // must agree with what D3D12ShaderModule produces when it walks the SPIR-V resources of that
  // stage in declaration order.
  std::vector<uint8_t> ubVertexRegister(descriptor.layout.uniformBlocks.size(), 0xFF);
  std::vector<uint8_t> ubFragmentRegister(descriptor.layout.uniformBlocks.size(), 0xFF);
  uint32_t nextVertexRegister = 0;
  uint32_t nextFragmentRegister = 0;
  for (size_t i = 0; i < descriptor.layout.uniformBlocks.size(); i++) {
    const auto& entry = descriptor.layout.uniformBlocks[i];
    if (entry.visibility & ShaderVisibility::Vertex) {
      ubVertexRegister[i] = static_cast<uint8_t>(nextVertexRegister++);
    }
    if (entry.visibility & ShaderVisibility::Fragment) {
      ubFragmentRegister[i] = static_cast<uint8_t>(nextFragmentRegister++);
    }
  }

  uint32_t paramCursor = 0;
  shapeKey.push_back(static_cast<uint8_t>(descriptor.layout.uniformBlocks.size()));
  for (size_t i = 0; i < descriptor.layout.uniformBlocks.size(); i++) {
    const auto& entry = descriptor.layout.uniformBlocks[i];
    uniformRootParameterIndex[entry.binding] = paramCursor++;
    uniformBlockVisibility[entry.binding] = entry.visibility;
    uniformBindingSet.insert(entry.binding);
    // Encode visibility plus per-stage register indices in the shape key. Different stage-local
    // register layouts must hit different cached root signatures; otherwise a pipeline whose
    // fragment UBO ends up at b1 (because it has a sibling at b0) would reuse another
    // pipeline's root signature that still places it at b0.
    shapeKey.push_back(static_cast<uint8_t>(entry.visibility & 0xFF));
    shapeKey.push_back(static_cast<uint8_t>((entry.visibility >> 8) & 0xFF));
    shapeKey.push_back(ubVertexRegister[i]);
    shapeKey.push_back(ubFragmentRegister[i]);
  }

  shapeKey.push_back(static_cast<uint8_t>(descriptor.layout.textureSamplers.size()));
  unsigned textureUnit = 0;
  for (const auto& entry : descriptor.layout.textureSamplers) {
    uint32_t srvParamIndex = paramCursor++;
    uint32_t samplerParamIndex = paramCursor++;
    textureRootParameterIndex[entry.binding] = srvParamIndex;
    samplerRootParameterIndex[entry.binding] = samplerParamIndex;
    textureUnits[entry.binding] = textureUnit++;
    textureBindingSet.insert(entry.binding);
    // Encode each sampler binding's visibility into the shape key. Without this two pipelines
    // that differ only in vertex/fragment-only sampler visibility would collide on the cached
    // root signature once the SRV/Sampler root parameters below honour entry.visibility.
    shapeKey.push_back(static_cast<uint8_t>(entry.visibility & 0xFF));
    shapeKey.push_back(static_cast<uint8_t>((entry.visibility >> 8) & 0xFF));
  }

  // Cache hit: reuse the existing D3D12 root signature object. Different pipelines sharing the
  // same binding shape (e.g. all single-texture fragment-only shaders) end up referencing one
  // ID3D12RootSignature, saving SerializeRootSignature + CreateRootSignature on every PSO.
  if (auto cached = gpu->findRootSignature(shapeKey); cached != nullptr) {
    rootSignature = std::move(cached);
#ifdef TGFX_D3D12_PERF_TRACE
    LOGI("[D3D12-Perf] RootSignature   HIT  (shape=%zuB)", shapeKey.size());
#endif
    return true;
  }
#ifdef TGFX_D3D12_PERF_TRACE
  auto rsT0 = Clock::Now();
#endif

  // Cache miss: build the root signature description from scratch and serialise it.
  std::vector<D3D12_ROOT_PARAMETER> rootParameters;
  // Each texture binding contributes two descriptor tables (SRV + Sampler) that live in different
  // descriptor heap types. They cannot share one D3D12_ROOT_PARAMETER because each table can only
  // reference a single heap. We therefore store each range in its own array entry; the
  // D3D12_ROOT_PARAMETER references the array by pointer, so the storage must outlive the
  // SerializeRootSignature call. reserve() keeps pointers stable across emplace_back().
  std::vector<D3D12_DESCRIPTOR_RANGE> srvRanges;
  std::vector<D3D12_DESCRIPTOR_RANGE> samplerRanges;
  srvRanges.reserve(descriptor.layout.textureSamplers.size());
  samplerRanges.reserve(descriptor.layout.textureSamplers.size());

  for (size_t i = 0; i < descriptor.layout.uniformBlocks.size(); i++) {
    const auto& entry = descriptor.layout.uniformBlocks[i];
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.ShaderVisibility = ToD3D12ShaderVisibility(entry.visibility);
    // ShaderRegister is per-stage in HLSL. Pick the register index from whichever stage the
    // entry is visible to; for VertexFragment-visible UBOs the two stage-local indices must
    // match, otherwise the single CBV root parameter cannot satisfy both stages with one
    // register number. Such a configuration is rejected here so the mismatch surfaces early
    // instead of producing silently broken bindings.
    uint8_t vsReg = ubVertexRegister[i];
    uint8_t fsReg = ubFragmentRegister[i];
    if (vsReg != 0xFF && fsReg != 0xFF && vsReg != fsReg) {
      LOGE(
          "D3D12RenderPipeline: VertexFragment-visible UBO binding %u cannot share a single CBV "
          "root parameter when its vertex-stage register (b%u) and fragment-stage register (b%u) "
          "differ. Either split it into vertex-only and fragment-only entries, or extend the "
          "root signature to emit two CBV root parameters for this binding.",
          entry.binding, static_cast<unsigned>(vsReg), static_cast<unsigned>(fsReg));
      return false;
    }
    param.Descriptor.ShaderRegister = (vsReg != 0xFF) ? vsReg : fsReg;
    param.Descriptor.RegisterSpace = 0;
    rootParameters.push_back(param);
  }

  unsigned rangeRegister = 0;
  for (const auto& entry : descriptor.layout.textureSamplers) {
    auto& srvRange = srvRanges.emplace_back();
    srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = rangeRegister;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER srvParam = {};
    srvParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    // Honour the caller-declared visibility instead of forcing pixel-only. Vertex texture
    // sampling (noise / displacement / geometry LOD lookups) needs SRVs visible to the vertex
    // stage; the per-entry shapeKey above already partitions the cache so different visibility
    // shapes do not collide.
    srvParam.ShaderVisibility = ToD3D12ShaderVisibility(entry.visibility);
    srvParam.DescriptorTable.NumDescriptorRanges = 1;
    srvParam.DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameters.push_back(srvParam);

    auto& samplerRange = samplerRanges.emplace_back();
    samplerRange = {};
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = 1;
    samplerRange.BaseShaderRegister = rangeRegister;
    samplerRange.RegisterSpace = 0;
    samplerRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER samplerParam = {};
    samplerParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    samplerParam.ShaderVisibility = ToD3D12ShaderVisibility(entry.visibility);
    samplerParam.DescriptorTable.NumDescriptorRanges = 1;
    samplerParam.DescriptorTable.pDescriptorRanges = &samplerRange;
    rootParameters.push_back(samplerParam);

    rangeRegister++;
  }

  D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
  rootSigDesc.NumParameters = static_cast<UINT>(rootParameters.size());
  rootSigDesc.pParameters = rootParameters.empty() ? nullptr : rootParameters.data();
  rootSigDesc.NumStaticSamplers = 0;
  rootSigDesc.pStaticSamplers = nullptr;
  rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> blob = nullptr;
  ComPtr<ID3DBlob> errorBlob = nullptr;
  auto hr =
      D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errorBlob);
  if (FAILED(hr)) {
    if (errorBlob != nullptr) {
      LOGE("D3D12RenderPipeline: D3D12SerializeRootSignature failed (HRESULT=0x%08X): %s",
           static_cast<unsigned>(hr), static_cast<const char*>(errorBlob->GetBufferPointer()));
    } else {
      LOGE("D3D12RenderPipeline: D3D12SerializeRootSignature failed (HRESULT=0x%08X).",
           static_cast<unsigned>(hr));
    }
    return false;
  }

  hr = gpu->device()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                          IID_PPV_ARGS(&rootSignature));
  if (FAILED(hr)) {
    LOGE("D3D12RenderPipeline: CreateRootSignature failed (HRESULT=0x%08X).",
         static_cast<unsigned>(hr));
    rootSignature = nullptr;
    return false;
  }
  // Publish the freshly-built root signature so subsequent pipelines with the same shape hit
  // the cache. The map keeps an additional ComPtr reference; the pipeline retains its own
  // reference via the rootSignature member, so the object outlives whichever owner is dropped
  // first.
  gpu->cacheRootSignature(std::move(shapeKey), rootSignature);
#ifdef TGFX_D3D12_PERF_TRACE
  LOGI("[D3D12-Perf] RootSignature   MISS build=%lluus",
       static_cast<unsigned long long>(Clock::Now() - rsT0));
#endif
  return true;
}

bool D3D12RenderPipeline::createPipelineState(D3D12GPU* gpu,
                                              const RenderPipelineDescriptor& descriptor) {
  if (!descriptor.vertex.module || !descriptor.fragment.module) {
    LOGE("D3D12RenderPipeline: vertex or fragment shader module is missing.");
    return false;
  }
  auto vertexShader = std::static_pointer_cast<D3D12ShaderModule>(descriptor.vertex.module);
  auto fragmentShader = std::static_pointer_cast<D3D12ShaderModule>(descriptor.fragment.module);
  auto vsBytecode = vertexShader->shaderBytecode();
  auto psBytecode = fragmentShader->shaderBytecode();
  if (vsBytecode.pShaderBytecode == nullptr || psBytecode.pShaderBytecode == nullptr) {
    LOGE("D3D12RenderPipeline: shader module produced empty bytecode.");
    return false;
  }

  // Vertex input layout. Semantic names match the SPIRV-Cross HLSL convention of TEXCOORD{N},
  // where N is the SPIR-V input location assigned by ShaderCompiler::PreprocessGLSL().
  std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
  uint32_t globalLocation = 0;
  vertexStrides.assign(descriptor.vertex.bufferLayouts.size(), 0);
  for (uint32_t i = 0; i < static_cast<uint32_t>(descriptor.vertex.bufferLayouts.size()); i++) {
    const auto& layout = descriptor.vertex.bufferLayouts[i];
    uint32_t offset = 0;
    for (const auto& attr : layout.attributes) {
      D3D12_INPUT_ELEMENT_DESC element = {};
      element.SemanticName = "TEXCOORD";
      element.SemanticIndex = globalLocation++;
      element.Format = ToD3D12VertexFormat(attr.format());
      element.InputSlot = i;
      element.AlignedByteOffset = offset;
      element.InputSlotClass = (layout.stepMode == VertexStepMode::Instance)
                                   ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                                   : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
      element.InstanceDataStepRate = (layout.stepMode == VertexStepMode::Instance) ? 1 : 0;
      inputElements.push_back(element);
      offset += static_cast<uint32_t>(attr.size());
    }
    // Fall back to the computed attribute total when the descriptor leaves stride at zero, which
    // is the same convention the Vulkan/Metal backends use.
    vertexStrides[i] = static_cast<uint32_t>(layout.stride > 0 ? layout.stride : offset);
  }

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = rootSignature.Get();
  psoDesc.VS = vsBytecode;
  psoDesc.PS = psBytecode;

  // Blend state — one entry per color attachment.
  psoDesc.BlendState.AlphaToCoverageEnable =
      descriptor.multisample.alphaToCoverageEnabled ? TRUE : FALSE;
  psoDesc.BlendState.IndependentBlendEnable =
      (descriptor.fragment.colorAttachments.size() > 1) ? TRUE : FALSE;
  for (size_t i = 0; i < descriptor.fragment.colorAttachments.size() &&
                     i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
       i++) {
    const auto& ca = descriptor.fragment.colorAttachments[i];
    auto& rt = psoDesc.BlendState.RenderTarget[i];
    rt.BlendEnable = ca.blendEnable ? TRUE : FALSE;
    rt.LogicOpEnable = FALSE;
    rt.SrcBlend = ToD3D12BlendFactor(ca.srcColorBlendFactor);
    rt.DestBlend = ToD3D12BlendFactor(ca.dstColorBlendFactor);
    rt.BlendOp = ToD3D12BlendOperation(ca.colorBlendOp);
    rt.SrcBlendAlpha = ToD3D12BlendFactorAlpha(ca.srcAlphaBlendFactor);
    rt.DestBlendAlpha = ToD3D12BlendFactorAlpha(ca.dstAlphaBlendFactor);
    rt.BlendOpAlpha = ToD3D12BlendOperation(ca.alphaBlendOp);
    rt.LogicOp = D3D12_LOGIC_OP_NOOP;
    rt.RenderTargetWriteMask = ToD3D12RenderTargetWriteMask(ca.colorWriteMask);
  }

  psoDesc.SampleMask = descriptor.multisample.mask;

  // Rasterizer.
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode = ToD3D12CullMode(descriptor.primitive.cullMode);
  psoDesc.RasterizerState.FrontCounterClockwise =
      ToD3D12FrontCounterClockwise(descriptor.primitive.frontFace) ? TRUE : FALSE;
  psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
  psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
  psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
  psoDesc.RasterizerState.DepthClipEnable = TRUE;
  psoDesc.RasterizerState.MultisampleEnable = (descriptor.multisample.count > 1) ? TRUE : FALSE;
  psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
  psoDesc.RasterizerState.ForcedSampleCount = 0;
  psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

  // Depth-stencil. Depth test follows the same enable predicate as VulkanRenderPipeline.
  bool depthTestEnable = (descriptor.depthStencil.depthCompare != CompareFunction::Always) ||
                         descriptor.depthStencil.depthWriteEnabled;
  psoDesc.DepthStencilState.DepthEnable = depthTestEnable ? TRUE : FALSE;
  psoDesc.DepthStencilState.DepthWriteMask = descriptor.depthStencil.depthWriteEnabled
                                                 ? D3D12_DEPTH_WRITE_MASK_ALL
                                                 : D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.DepthStencilState.DepthFunc =
      ToD3D12CompareFunction(descriptor.depthStencil.depthCompare);
  psoDesc.DepthStencilState.StencilEnable =
      HasNonTrivialStencilState(descriptor.depthStencil) ? TRUE : FALSE;
  psoDesc.DepthStencilState.StencilReadMask =
      static_cast<UINT8>(descriptor.depthStencil.stencilReadMask);
  psoDesc.DepthStencilState.StencilWriteMask =
      static_cast<UINT8>(descriptor.depthStencil.stencilWriteMask);
  psoDesc.DepthStencilState.FrontFace.StencilFailOp =
      ToD3D12StencilOperation(descriptor.depthStencil.stencilFront.failOp);
  psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp =
      ToD3D12StencilOperation(descriptor.depthStencil.stencilFront.depthFailOp);
  psoDesc.DepthStencilState.FrontFace.StencilPassOp =
      ToD3D12StencilOperation(descriptor.depthStencil.stencilFront.passOp);
  psoDesc.DepthStencilState.FrontFace.StencilFunc =
      ToD3D12CompareFunction(descriptor.depthStencil.stencilFront.compare);
  psoDesc.DepthStencilState.BackFace.StencilFailOp =
      ToD3D12StencilOperation(descriptor.depthStencil.stencilBack.failOp);
  psoDesc.DepthStencilState.BackFace.StencilDepthFailOp =
      ToD3D12StencilOperation(descriptor.depthStencil.stencilBack.depthFailOp);
  psoDesc.DepthStencilState.BackFace.StencilPassOp =
      ToD3D12StencilOperation(descriptor.depthStencil.stencilBack.passOp);
  psoDesc.DepthStencilState.BackFace.StencilFunc =
      ToD3D12CompareFunction(descriptor.depthStencil.stencilBack.compare);

  psoDesc.InputLayout.pInputElementDescs = inputElements.empty() ? nullptr : inputElements.data();
  psoDesc.InputLayout.NumElements = static_cast<UINT>(inputElements.size());
  // Strip cut and topology type live on the PSO in D3D12, but tgfx exposes IndexFormat and
  // PrimitiveType as per-draw-call state (RenderPass::setIndexBuffer / RenderPass::draw)
  // rather than fields on RenderPipelineDescriptor. The two values below therefore must be
  // chosen at PSO creation time without knowing what the eventual draws look like, so we hard
  // code them to the only combination tgfx ever uses:
  //   * IBStripCutValue=DISABLED — matches Vulkan, which sets primitiveRestartEnable=false on
  //     its PSOs. No tgfx draw op relies on 0xFFFF/0xFFFFFFFF restarting a strip.
  //   * PrimitiveTopologyType=TRIANGLE — tgfx's PrimitiveType only carries Triangles and
  //     TriangleStrip today and ToD3D12PrimitiveTopologyType already collapses both onto
  //     TRIANGLE. Once tgfx adds LINE/POINT (or moves these fields onto PrimitiveDescriptor)
  //     this branch must be revisited together with the matching IASetPrimitiveTopology call
  //     in D3D12RenderPass; until then a single PSO topology type covers every draw call.
  psoDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = static_cast<UINT>(descriptor.fragment.colorAttachments.size());
  for (size_t i = 0; i < descriptor.fragment.colorAttachments.size() &&
                     i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
       i++) {
    psoDesc.RTVFormats[i] = static_cast<DXGI_FORMAT>(
        gpu->getDXGIFormat(descriptor.fragment.colorAttachments[i].format));
  }
  psoDesc.DSVFormat =
      (descriptor.depthStencil.format != PixelFormat::Unknown)
          ? static_cast<DXGI_FORMAT>(gpu->getDXGIFormat(descriptor.depthStencil.format))
          : static_cast<DXGI_FORMAT>(DXGI_FORMAT_UNKNOWN);
  psoDesc.SampleDesc.Count = static_cast<UINT>(descriptor.multisample.count);
  psoDesc.SampleDesc.Quality = 0;
  psoDesc.NodeMask = 0;
  psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

#ifdef TGFX_D3D12_PERF_TRACE
  auto psoT0 = Clock::Now();
#endif
  auto hr = gpu->device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
#ifdef TGFX_D3D12_PERF_TRACE
  LOGI("[D3D12-Perf] CreateGraphicsPipelineState=%lluus rt=%u msaa=%u",
       static_cast<unsigned long long>(Clock::Now() - psoT0), psoDesc.NumRenderTargets,
       psoDesc.SampleDesc.Count);
#endif
  if (FAILED(hr)) {
    LOGE("D3D12RenderPipeline: CreateGraphicsPipelineState failed (HRESULT=0x%08X).",
         static_cast<unsigned>(hr));
#ifdef TGFX_D3D12_DEBUG_LAYER
    // Surface debug-layer messages so the underlying validation issue is visible. These are
    // queued by the runtime when EnableDebugLayer was called before device creation.
    ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
    if (SUCCEEDED(gpu->device()->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
      auto count = infoQueue->GetNumStoredMessages();
      for (UINT64 i = 0; i < count; i++) {
        SIZE_T msgLen = 0;
        infoQueue->GetMessage(i, nullptr, &msgLen);
        std::vector<char> buf(msgLen);
        auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (SUCCEEDED(infoQueue->GetMessage(i, msg, &msgLen))) {
          LOGE("  D3D12 message: %.*s", static_cast<int>(msg->DescriptionByteLength),
               msg->pDescription);
        }
      }
      infoQueue->ClearStoredMessages();
    }
#endif
    pipelineState = nullptr;
    return false;
  }
  return true;
}

}  // namespace tgfx
