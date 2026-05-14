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

#include "D3D12Util.h"
#include "tgfx/gpu/GPUBuffer.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {

D3D12_PRIMITIVE_TOPOLOGY ToD3D12PrimitiveTopology(PrimitiveType primitiveType) {
  switch (primitiveType) {
    case PrimitiveType::Triangles:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveType::TriangleStrip:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    default:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  }
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE ToD3D12PrimitiveTopologyType(PrimitiveType primitiveType) {
  switch (primitiveType) {
    case PrimitiveType::Triangles:
    case PrimitiveType::TriangleStrip:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    default:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  }
}

DXGI_FORMAT ToD3D12VertexFormat(VertexFormat format) {
  switch (format) {
    case VertexFormat::Float:
      return DXGI_FORMAT_R32_FLOAT;
    case VertexFormat::Float2:
      return DXGI_FORMAT_R32G32_FLOAT;
    case VertexFormat::Float3:
      return DXGI_FORMAT_R32G32B32_FLOAT;
    case VertexFormat::Float4:
      return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case VertexFormat::Half:
      return DXGI_FORMAT_R16_FLOAT;
    case VertexFormat::Half2:
      return DXGI_FORMAT_R16G16_FLOAT;
    case VertexFormat::Half3:
      // D3D12 does not have a native R16G16B16_FLOAT format. Use R16G16B16A16_FLOAT as fallback.
      return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case VertexFormat::Half4:
      return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case VertexFormat::Int:
      return DXGI_FORMAT_R32_SINT;
    case VertexFormat::Int2:
      return DXGI_FORMAT_R32G32_SINT;
    case VertexFormat::Int3:
      return DXGI_FORMAT_R32G32B32_SINT;
    case VertexFormat::Int4:
      return DXGI_FORMAT_R32G32B32A32_SINT;
    case VertexFormat::UByteNormalized:
      return DXGI_FORMAT_R8_UNORM;
    case VertexFormat::UByte2Normalized:
      return DXGI_FORMAT_R8G8_UNORM;
    case VertexFormat::UByte3Normalized:
      // D3D12 does not have a native R8G8B8_UNORM vertex format. Use R8G8B8A8_UNORM as fallback.
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VertexFormat::UByte4Normalized:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    default:
      return DXGI_FORMAT_R32_FLOAT;
  }
}

D3D12_COMPARISON_FUNC ToD3D12CompareFunction(CompareFunction compareFunction) {
  switch (compareFunction) {
    case CompareFunction::Never:
      return D3D12_COMPARISON_FUNC_NEVER;
    case CompareFunction::Less:
      return D3D12_COMPARISON_FUNC_LESS;
    case CompareFunction::Equal:
      return D3D12_COMPARISON_FUNC_EQUAL;
    case CompareFunction::LessEqual:
      return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case CompareFunction::Greater:
      return D3D12_COMPARISON_FUNC_GREATER;
    case CompareFunction::NotEqual:
      return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case CompareFunction::GreaterEqual:
      return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case CompareFunction::Always:
      return D3D12_COMPARISON_FUNC_ALWAYS;
    default:
      return D3D12_COMPARISON_FUNC_ALWAYS;
  }
}

D3D12_STENCIL_OP ToD3D12StencilOperation(StencilOperation stencilOp) {
  switch (stencilOp) {
    case StencilOperation::Keep:
      return D3D12_STENCIL_OP_KEEP;
    case StencilOperation::Zero:
      return D3D12_STENCIL_OP_ZERO;
    case StencilOperation::Replace:
      return D3D12_STENCIL_OP_REPLACE;
    case StencilOperation::Invert:
      return D3D12_STENCIL_OP_INVERT;
    case StencilOperation::IncrementClamp:
      return D3D12_STENCIL_OP_INCR_SAT;
    case StencilOperation::DecrementClamp:
      return D3D12_STENCIL_OP_DECR_SAT;
    case StencilOperation::IncrementWrap:
      return D3D12_STENCIL_OP_INCR;
    case StencilOperation::DecrementWrap:
      return D3D12_STENCIL_OP_DECR;
    default:
      return D3D12_STENCIL_OP_KEEP;
  }
}

D3D12_BLEND ToD3D12BlendFactor(BlendFactor blendFactor) {
  switch (blendFactor) {
    case BlendFactor::Zero:
      return D3D12_BLEND_ZERO;
    case BlendFactor::One:
      return D3D12_BLEND_ONE;
    case BlendFactor::Src:
      return D3D12_BLEND_SRC_COLOR;
    case BlendFactor::OneMinusSrc:
      return D3D12_BLEND_INV_SRC_COLOR;
    case BlendFactor::Dst:
      return D3D12_BLEND_DEST_COLOR;
    case BlendFactor::OneMinusDst:
      return D3D12_BLEND_INV_DEST_COLOR;
    case BlendFactor::SrcAlpha:
      return D3D12_BLEND_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
      return D3D12_BLEND_INV_SRC_ALPHA;
    case BlendFactor::DstAlpha:
      return D3D12_BLEND_DEST_ALPHA;
    case BlendFactor::OneMinusDstAlpha:
      return D3D12_BLEND_INV_DEST_ALPHA;
    case BlendFactor::Src1:
      return D3D12_BLEND_SRC1_COLOR;
    case BlendFactor::OneMinusSrc1:
      return D3D12_BLEND_INV_SRC1_COLOR;
    case BlendFactor::Src1Alpha:
      return D3D12_BLEND_SRC1_ALPHA;
    case BlendFactor::OneMinusSrc1Alpha:
      return D3D12_BLEND_INV_SRC1_ALPHA;
    default:
      return D3D12_BLEND_ONE;
  }
}

D3D12_BLEND ToD3D12BlendFactorAlpha(BlendFactor blendFactor) {
  // Alpha blend factors must use the *_ALPHA variants. D3D11/12 validation rejects color factors
  // (SRC_COLOR / INV_SRC_COLOR / DEST_COLOR / INV_DEST_COLOR / SRC1_COLOR / INV_SRC1_COLOR) when
  // they appear in SrcBlendAlpha or DestBlendAlpha — color and alpha are independent channels.
  switch (blendFactor) {
    case BlendFactor::Src:
      return D3D12_BLEND_SRC_ALPHA;
    case BlendFactor::OneMinusSrc:
      return D3D12_BLEND_INV_SRC_ALPHA;
    case BlendFactor::Dst:
      return D3D12_BLEND_DEST_ALPHA;
    case BlendFactor::OneMinusDst:
      return D3D12_BLEND_INV_DEST_ALPHA;
    case BlendFactor::Src1:
      return D3D12_BLEND_SRC1_ALPHA;
    case BlendFactor::OneMinusSrc1:
      return D3D12_BLEND_INV_SRC1_ALPHA;
    default:
      return ToD3D12BlendFactor(blendFactor);
  }
}

D3D12_BLEND_OP ToD3D12BlendOperation(BlendOperation blendOp) {
  switch (blendOp) {
    case BlendOperation::Add:
      return D3D12_BLEND_OP_ADD;
    case BlendOperation::Subtract:
      return D3D12_BLEND_OP_SUBTRACT;
    case BlendOperation::ReverseSubtract:
      return D3D12_BLEND_OP_REV_SUBTRACT;
    case BlendOperation::Min:
      return D3D12_BLEND_OP_MIN;
    case BlendOperation::Max:
      return D3D12_BLEND_OP_MAX;
    default:
      return D3D12_BLEND_OP_ADD;
  }
}

D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(AddressMode addressMode) {
  switch (addressMode) {
    case AddressMode::ClampToEdge:
      return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case AddressMode::Repeat:
      return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case AddressMode::MirrorRepeat:
      return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case AddressMode::ClampToBorder:
      return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    default:
      return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  }
}

D3D12_FILTER ToD3D12Filter(FilterMode minFilter, FilterMode magFilter, MipmapMode mipmapMode) {
  bool minLinear = (minFilter == FilterMode::Linear);
  bool magLinear = (magFilter == FilterMode::Linear);
  bool mipLinear = (mipmapMode == MipmapMode::Linear);
  bool mipEnabled = (mipmapMode != MipmapMode::None);

  if (!mipEnabled) {
    if (minLinear && magLinear) {
      return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    }
    if (minLinear) {
      return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    }
    if (magLinear) {
      return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
    }
    return D3D12_FILTER_MIN_MAG_MIP_POINT;
  }

  if (minLinear && magLinear && mipLinear) {
    return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  }
  if (minLinear && magLinear) {
    return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  }
  if (minLinear && mipLinear) {
    return D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
  }
  if (magLinear && mipLinear) {
    return D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;
  }
  if (minLinear) {
    return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
  }
  if (magLinear) {
    return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
  }
  if (mipLinear) {
    return D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
  }
  return D3D12_FILTER_MIN_MAG_MIP_POINT;
}

D3D12_CULL_MODE ToD3D12CullMode(CullMode cullMode) {
  switch (cullMode) {
    case CullMode::None:
      return D3D12_CULL_MODE_NONE;
    case CullMode::Front:
      return D3D12_CULL_MODE_FRONT;
    case CullMode::Back:
      return D3D12_CULL_MODE_BACK;
  }
  return D3D12_CULL_MODE_NONE;
}

bool ToD3D12FrontCounterClockwise(FrontFace frontFace) {
  switch (frontFace) {
    case FrontFace::CCW:
      return true;
    case FrontFace::CW:
      return false;
  }
  return true;
}

D3D12_INDEX_BUFFER_STRIP_CUT_VALUE ToD3D12StripCutValue(IndexFormat indexFormat) {
  switch (indexFormat) {
    case IndexFormat::UInt16:
      return D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
    case IndexFormat::UInt32:
      return D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
  }
  return D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
}

void TransitionResourceState(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                             D3D12_RESOURCE_STATES oldState, D3D12_RESOURCE_STATES newState,
                             UINT subresource) {
  if (commandList == nullptr || resource == nullptr || oldState == newState) {
    return;
  }
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = oldState;
  barrier.Transition.StateAfter = newState;
  barrier.Transition.Subresource = subresource;
  commandList->ResourceBarrier(1, &barrier);
}

}  // namespace tgfx
