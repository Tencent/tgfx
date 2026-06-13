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

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "tgfx/gpu/Attribute.h"
#include "tgfx/gpu/BlendFactor.h"
#include "tgfx/gpu/BlendOperation.h"
#include "tgfx/gpu/CompareFunction.h"
#include "tgfx/gpu/FilterMode.h"
#include "tgfx/gpu/MipmapMode.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/Sampler.h"
#include "tgfx/gpu/StencilOperation.h"

namespace tgfx {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

D3D12_PRIMITIVE_TOPOLOGY ToD3D12PrimitiveTopology(PrimitiveType primitiveType);

D3D12_PRIMITIVE_TOPOLOGY_TYPE ToD3D12PrimitiveTopologyType(PrimitiveType primitiveType);

DXGI_FORMAT ToD3D12VertexFormat(VertexFormat format);

D3D12_COMPARISON_FUNC ToD3D12CompareFunction(CompareFunction compareFunction);

D3D12_STENCIL_OP ToD3D12StencilOperation(StencilOperation stencilOp);

D3D12_BLEND ToD3D12BlendFactor(BlendFactor blendFactor);

/**
 * Like ToD3D12BlendFactor() but rewrites the four COLOR-only D3D12 blend factors (SRC_COLOR,
 * INV_SRC_COLOR, DEST_COLOR, INV_DEST_COLOR, plus their dual-source variants) into their ALPHA
 * counterparts. CreateBlendState validation rejects color factors when applied to the alpha
 * channel, so this helper must be used for {Src,Dest}BlendAlpha.
 */
D3D12_BLEND ToD3D12BlendFactorAlpha(BlendFactor blendFactor);

D3D12_BLEND_OP ToD3D12BlendOperation(BlendOperation blendOp);

D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(AddressMode addressMode);

D3D12_FILTER ToD3D12Filter(FilterMode minFilter, FilterMode magFilter, MipmapMode mipmapMode);

D3D12_CULL_MODE ToD3D12CullMode(CullMode cullMode);

bool ToD3D12FrontCounterClockwise(FrontFace frontFace);

D3D12_INDEX_BUFFER_STRIP_CUT_VALUE ToD3D12StripCutValue(IndexFormat indexFormat);

/**
 * Records a single ID3D12Resource::ResourceBarrier(TRANSITION) on the given command list. No-op
 * when oldState == newState. Used by every code path that needs to flip an ID3D12Resource between
 * read- and write-only states (RTV/DSV setup, copy commands, shader sampling, etc.).
 */
void TransitionResourceState(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                             D3D12_RESOURCE_STATES oldState, D3D12_RESOURCE_STATES newState,
                             UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

}  // namespace tgfx
