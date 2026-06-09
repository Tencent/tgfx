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

#include "WebGPUUtil.h"
#include "core/utils/Log.h"
#include "tgfx/gpu/ColorWriteMask.h"
#include "tgfx/gpu/GPUBuffer.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {

WGPUTextureUsageFlags ToWGPUTextureUsage(uint32_t usage) {
  WGPUTextureUsageFlags flags = WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
  if (usage & TextureUsage::TEXTURE_BINDING) {
    flags |= WGPUTextureUsage_TextureBinding;
  }
  if (usage & TextureUsage::RENDER_ATTACHMENT) {
    flags |= WGPUTextureUsage_RenderAttachment;
  }
  return flags;
}

WGPUBufferUsageFlags ToWGPUBufferUsage(uint32_t usage) {
  WGPUBufferUsageFlags flags = WGPUBufferUsage_CopyDst;
  if (usage & GPUBufferUsage::INDEX) {
    flags |= WGPUBufferUsage_Index;
  }
  if (usage & GPUBufferUsage::VERTEX) {
    flags |= WGPUBufferUsage_Vertex;
  }
  if (usage & GPUBufferUsage::UNIFORM) {
    flags |= WGPUBufferUsage_Uniform;
  }
  if (usage & GPUBufferUsage::READBACK) {
    flags = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
  }
  return flags;
}

WGPUPrimitiveTopology ToWGPUPrimitiveTopology(PrimitiveType primitiveType) {
  switch (primitiveType) {
    case PrimitiveType::Triangles:
      return WGPUPrimitiveTopology_TriangleList;
    case PrimitiveType::TriangleStrip:
      return WGPUPrimitiveTopology_TriangleStrip;
  }
  return WGPUPrimitiveTopology_TriangleList;
}

WGPUVertexFormat ToWGPUVertexFormat(VertexFormat format) {
  switch (format) {
    case VertexFormat::Float:
      return WGPUVertexFormat_Float32;
    case VertexFormat::Float2:
      return WGPUVertexFormat_Float32x2;
    case VertexFormat::Float3:
      return WGPUVertexFormat_Float32x3;
    case VertexFormat::Float4:
      return WGPUVertexFormat_Float32x4;
    case VertexFormat::Half:
      LOGE("WebGPU has no Float16x1 format; Half is not correctly supported, using Float16x2");
      return WGPUVertexFormat_Float16x2;
    case VertexFormat::Half2:
      return WGPUVertexFormat_Float16x2;
    case VertexFormat::Half3:
      LOGE("WebGPU has no Float16x3 format; Half3 is not correctly supported, using Float16x4");
      return WGPUVertexFormat_Float16x4;
    case VertexFormat::Half4:
      return WGPUVertexFormat_Float16x4;
    case VertexFormat::Int:
      return WGPUVertexFormat_Sint32;
    case VertexFormat::Int2:
      return WGPUVertexFormat_Sint32x2;
    case VertexFormat::Int3:
      return WGPUVertexFormat_Sint32x3;
    case VertexFormat::Int4:
      return WGPUVertexFormat_Sint32x4;
    case VertexFormat::UByteNormalized:
      LOGE(
          "WebGPU has no Unorm8x1 format; UByteNormalized is not correctly supported, using "
          "Unorm8x2");
      return WGPUVertexFormat_Unorm8x2;
    case VertexFormat::UByte2Normalized:
      return WGPUVertexFormat_Unorm8x2;
    case VertexFormat::UByte3Normalized:
      LOGE(
          "WebGPU has no Unorm8x3 format; UByte3Normalized is not correctly supported, using "
          "Unorm8x4");
      return WGPUVertexFormat_Unorm8x4;
    case VertexFormat::UByte4Normalized:
      return WGPUVertexFormat_Unorm8x4;
  }
  return WGPUVertexFormat_Float32;
}

WGPUCompareFunction ToWGPUCompareFunction(CompareFunction compareFunction) {
  switch (compareFunction) {
    case CompareFunction::Never:
      return WGPUCompareFunction_Never;
    case CompareFunction::Less:
      return WGPUCompareFunction_Less;
    case CompareFunction::Equal:
      return WGPUCompareFunction_Equal;
    case CompareFunction::LessEqual:
      return WGPUCompareFunction_LessEqual;
    case CompareFunction::Greater:
      return WGPUCompareFunction_Greater;
    case CompareFunction::NotEqual:
      return WGPUCompareFunction_NotEqual;
    case CompareFunction::GreaterEqual:
      return WGPUCompareFunction_GreaterEqual;
    case CompareFunction::Always:
      return WGPUCompareFunction_Always;
  }
  return WGPUCompareFunction_Always;
}

WGPUStencilOperation ToWGPUStencilOperation(StencilOperation stencilOp) {
  switch (stencilOp) {
    case StencilOperation::Keep:
      return WGPUStencilOperation_Keep;
    case StencilOperation::Zero:
      return WGPUStencilOperation_Zero;
    case StencilOperation::Replace:
      return WGPUStencilOperation_Replace;
    case StencilOperation::IncrementClamp:
      return WGPUStencilOperation_IncrementClamp;
    case StencilOperation::DecrementClamp:
      return WGPUStencilOperation_DecrementClamp;
    case StencilOperation::Invert:
      return WGPUStencilOperation_Invert;
    case StencilOperation::IncrementWrap:
      return WGPUStencilOperation_IncrementWrap;
    case StencilOperation::DecrementWrap:
      return WGPUStencilOperation_DecrementWrap;
  }
  return WGPUStencilOperation_Keep;
}

WGPUBlendFactor ToWGPUBlendFactor(BlendFactor blendFactor) {
  switch (blendFactor) {
    case BlendFactor::Zero:
      return WGPUBlendFactor_Zero;
    case BlendFactor::One:
      return WGPUBlendFactor_One;
    case BlendFactor::Src:
      return WGPUBlendFactor_Src;
    case BlendFactor::OneMinusSrc:
      return WGPUBlendFactor_OneMinusSrc;
    case BlendFactor::Dst:
      return WGPUBlendFactor_Dst;
    case BlendFactor::OneMinusDst:
      return WGPUBlendFactor_OneMinusDst;
    case BlendFactor::SrcAlpha:
      return WGPUBlendFactor_SrcAlpha;
    case BlendFactor::OneMinusSrcAlpha:
      return WGPUBlendFactor_OneMinusSrcAlpha;
    case BlendFactor::DstAlpha:
      return WGPUBlendFactor_DstAlpha;
    case BlendFactor::OneMinusDstAlpha:
      return WGPUBlendFactor_OneMinusDstAlpha;
    case BlendFactor::Src1:
      return WGPUBlendFactor_Src;
    case BlendFactor::OneMinusSrc1:
      return WGPUBlendFactor_OneMinusSrc;
    case BlendFactor::Src1Alpha:
      return WGPUBlendFactor_SrcAlpha;
    case BlendFactor::OneMinusSrc1Alpha:
      return WGPUBlendFactor_OneMinusSrcAlpha;
  }
  return WGPUBlendFactor_One;
}

WGPUBlendOperation ToWGPUBlendOperation(BlendOperation blendOp) {
  switch (blendOp) {
    case BlendOperation::Add:
      return WGPUBlendOperation_Add;
    case BlendOperation::Subtract:
      return WGPUBlendOperation_Subtract;
    case BlendOperation::ReverseSubtract:
      return WGPUBlendOperation_ReverseSubtract;
    case BlendOperation::Min:
      return WGPUBlendOperation_Min;
    case BlendOperation::Max:
      return WGPUBlendOperation_Max;
  }
  return WGPUBlendOperation_Add;
}

WGPUAddressMode ToWGPUAddressMode(AddressMode addressMode) {
  switch (addressMode) {
    case AddressMode::ClampToEdge:
      return WGPUAddressMode_ClampToEdge;
    case AddressMode::Repeat:
      return WGPUAddressMode_Repeat;
    case AddressMode::MirrorRepeat:
      return WGPUAddressMode_MirrorRepeat;
    case AddressMode::ClampToBorder:
      // WebGPU does not support ClampToBorder, fall back to ClampToEdge.
      return WGPUAddressMode_ClampToEdge;
  }
  return WGPUAddressMode_ClampToEdge;
}

WGPUFilterMode ToWGPUFilterMode(FilterMode filter) {
  switch (filter) {
    case FilterMode::Nearest:
      return WGPUFilterMode_Nearest;
    case FilterMode::Linear:
      return WGPUFilterMode_Linear;
  }
  return WGPUFilterMode_Nearest;
}

WGPUMipmapFilterMode ToWGPUMipmapFilterMode(MipmapMode filter) {
  switch (filter) {
    case MipmapMode::None:
      return WGPUMipmapFilterMode_Nearest;
    case MipmapMode::Nearest:
      return WGPUMipmapFilterMode_Nearest;
    case MipmapMode::Linear:
      return WGPUMipmapFilterMode_Linear;
  }
  return WGPUMipmapFilterMode_Nearest;
}

size_t WGPUTextureFormatBytesPerPixel(WGPUTextureFormat format) {
  switch (format) {
    case WGPUTextureFormat_R8Unorm:
      return 1;
    case WGPUTextureFormat_RG8Unorm:
      return 2;
    case WGPUTextureFormat_RGBA8Unorm:
    case WGPUTextureFormat_RGBA8UnormSrgb:
    case WGPUTextureFormat_BGRA8Unorm:
    case WGPUTextureFormat_BGRA8UnormSrgb:
      return 4;
    default:
      return 4;
  }
}

WGPUTextureFormat ToWGPUTextureFormat(PixelFormat format) {
  switch (format) {
    case PixelFormat::ALPHA_8:
    case PixelFormat::GRAY_8:
      return WGPUTextureFormat_R8Unorm;
    case PixelFormat::RG_88:
      return WGPUTextureFormat_RG8Unorm;
    case PixelFormat::RGBA_8888:
      return WGPUTextureFormat_RGBA8Unorm;
    case PixelFormat::BGRA_8888:
      return WGPUTextureFormat_BGRA8Unorm;
    case PixelFormat::DEPTH24_STENCIL8:
      return WGPUTextureFormat_Depth24PlusStencil8;
    default:
      return WGPUTextureFormat_RGBA8Unorm;
  }
}

WGPULoadOp ToWGPULoadOp(LoadAction loadAction) {
  switch (loadAction) {
    case LoadAction::DontCare:
      // WebGPU does not have a DontCare load op. Use Clear as the closest equivalent.
      return WGPULoadOp_Clear;
    case LoadAction::Load:
      return WGPULoadOp_Load;
    case LoadAction::Clear:
      return WGPULoadOp_Clear;
  }
  return WGPULoadOp_Clear;
}

WGPUStoreOp ToWGPUStoreOp(StoreAction storeAction) {
  switch (storeAction) {
    case StoreAction::DontCare:
      return WGPUStoreOp_Discard;
    case StoreAction::Store:
      return WGPUStoreOp_Store;
  }
  return WGPUStoreOp_Store;
}

WGPUCullMode ToWGPUCullMode(CullMode cullMode) {
  switch (cullMode) {
    case CullMode::None:
      return WGPUCullMode_None;
    case CullMode::Front:
      return WGPUCullMode_Front;
    case CullMode::Back:
      return WGPUCullMode_Back;
  }
  return WGPUCullMode_None;
}

WGPUFrontFace ToWGPUFrontFace(FrontFace frontFace) {
  switch (frontFace) {
    case FrontFace::CW:
      return WGPUFrontFace_CW;
    case FrontFace::CCW:
      return WGPUFrontFace_CCW;
  }
  return WGPUFrontFace_CCW;
}

WGPUIndexFormat ToWGPUIndexFormat(IndexFormat format) {
  switch (format) {
    case IndexFormat::UInt16:
      return WGPUIndexFormat_Uint16;
    case IndexFormat::UInt32:
      return WGPUIndexFormat_Uint32;
  }
  return WGPUIndexFormat_Uint16;
}

WGPUColorWriteMaskFlags ToWGPUColorWriteMask(uint32_t mask) {
  WGPUColorWriteMaskFlags flags = WGPUColorWriteMask_None;
  if (mask & ColorWriteMask::RED) {
    flags |= WGPUColorWriteMask_Red;
  }
  if (mask & ColorWriteMask::GREEN) {
    flags |= WGPUColorWriteMask_Green;
  }
  if (mask & ColorWriteMask::BLUE) {
    flags |= WGPUColorWriteMask_Blue;
  }
  if (mask & ColorWriteMask::ALPHA) {
    flags |= WGPUColorWriteMask_Alpha;
  }
  return flags;
}

}  // namespace tgfx
