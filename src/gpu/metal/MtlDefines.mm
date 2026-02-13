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

#include "MtlDefines.h"
#include "tgfx/gpu/Attribute.h"
#include "tgfx/gpu/GPUBuffer.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/Texture.h"
#include "tgfx/gpu/CompareFunction.h"
#include "tgfx/gpu/StencilOperation.h"
#include "tgfx/gpu/BlendFactor.h"
#include "tgfx/gpu/BlendOperation.h"
#include "tgfx/gpu/Sampler.h"
#include "tgfx/gpu/FilterMode.h"

namespace tgfx {

MTLTextureUsage MtlDefines::ToMTLTextureUsage(uint32_t usage) {
  MTLTextureUsage mtlUsage = MTLTextureUsageUnknown;
  if (usage & TextureUsage::TEXTURE_BINDING) {
    mtlUsage |= MTLTextureUsageShaderRead;
  }
  if (usage & TextureUsage::RENDER_ATTACHMENT) {
    mtlUsage |= MTLTextureUsageRenderTarget;
  }
  return mtlUsage;
}

MTLResourceOptions MtlDefines::ToMTLResourceOptions(uint32_t) {
  // TODO: Use MTLResourceStorageModePrivate for non-READBACK buffers once a staging buffer upload
  // mechanism is implemented, to improve GPU-side performance.
  return MTLResourceStorageModeShared;
}

MTLPrimitiveType MtlDefines::ToMTLPrimitiveType(PrimitiveType primitiveType) {
  switch (primitiveType) {
    case PrimitiveType::Triangles:
      return MTLPrimitiveTypeTriangle;
    case PrimitiveType::TriangleStrip:
      return MTLPrimitiveTypeTriangleStrip;
    default:
      return MTLPrimitiveTypeTriangle;
  }
}

MTLVertexFormat MtlDefines::ToMTLVertexFormat(VertexFormat format) {
  switch (format) {
    case VertexFormat::Float:
      return MTLVertexFormatFloat;
    case VertexFormat::Float2:
      return MTLVertexFormatFloat2;
    case VertexFormat::Float3:
      return MTLVertexFormatFloat3;
    case VertexFormat::Float4:
      return MTLVertexFormatFloat4;
    case VertexFormat::Half:
      return MTLVertexFormatHalf;
    case VertexFormat::Half2:
      return MTLVertexFormatHalf2;
    case VertexFormat::Half3:
      return MTLVertexFormatHalf3;
    case VertexFormat::Half4:
      return MTLVertexFormatHalf4;
    case VertexFormat::Int:
      return MTLVertexFormatInt;
    case VertexFormat::Int2:
      return MTLVertexFormatInt2;
    case VertexFormat::Int3:
      return MTLVertexFormatInt3;
    case VertexFormat::Int4:
      return MTLVertexFormatInt4;
    case VertexFormat::UByteNormalized:
      return MTLVertexFormatUCharNormalized;
    case VertexFormat::UByte2Normalized:
      return MTLVertexFormatUChar2Normalized;
    case VertexFormat::UByte3Normalized:
      return MTLVertexFormatUChar3Normalized;
    case VertexFormat::UByte4Normalized:
      return MTLVertexFormatUChar4Normalized;
    default:
      return MTLVertexFormatFloat;
  }
}

MTLCompareFunction MtlDefines::ToMTLCompareFunction(CompareFunction compareFunction) {
  switch (compareFunction) {
    case CompareFunction::Never:
      return MTLCompareFunctionNever;
    case CompareFunction::Less:
      return MTLCompareFunctionLess;
    case CompareFunction::Equal:
      return MTLCompareFunctionEqual;
    case CompareFunction::LessEqual:
      return MTLCompareFunctionLessEqual;
    case CompareFunction::Greater:
      return MTLCompareFunctionGreater;
    case CompareFunction::NotEqual:
      return MTLCompareFunctionNotEqual;
    case CompareFunction::GreaterEqual:
      return MTLCompareFunctionGreaterEqual;
    case CompareFunction::Always:
      return MTLCompareFunctionAlways;
    default:
      return MTLCompareFunctionAlways;
  }
}

MTLStencilOperation MtlDefines::ToMTLStencilOperation(StencilOperation stencilOp) {
  switch (stencilOp) {
    case StencilOperation::Keep:
      return MTLStencilOperationKeep;
    case StencilOperation::Zero:
      return MTLStencilOperationZero;
    case StencilOperation::Replace:
      return MTLStencilOperationReplace;
    case StencilOperation::IncrementClamp:
      return MTLStencilOperationIncrementClamp;
    case StencilOperation::DecrementClamp:
      return MTLStencilOperationDecrementClamp;
    case StencilOperation::Invert:
      return MTLStencilOperationInvert;
    case StencilOperation::IncrementWrap:
      return MTLStencilOperationIncrementWrap;
    case StencilOperation::DecrementWrap:
      return MTLStencilOperationDecrementWrap;
    default:
      return MTLStencilOperationKeep;
  }
}

MTLBlendFactor MtlDefines::ToMTLBlendFactor(BlendFactor blendFactor) {
  switch (blendFactor) {
    case BlendFactor::Zero:
      return MTLBlendFactorZero;
    case BlendFactor::One:
      return MTLBlendFactorOne;
    case BlendFactor::Src:
      return MTLBlendFactorSourceColor;
    case BlendFactor::OneMinusSrc:
      return MTLBlendFactorOneMinusSourceColor;
    case BlendFactor::Dst:
      return MTLBlendFactorDestinationColor;
    case BlendFactor::OneMinusDst:
      return MTLBlendFactorOneMinusDestinationColor;
    case BlendFactor::SrcAlpha:
      return MTLBlendFactorSourceAlpha;
    case BlendFactor::OneMinusSrcAlpha:
      return MTLBlendFactorOneMinusSourceAlpha;
    case BlendFactor::DstAlpha:
      return MTLBlendFactorDestinationAlpha;
    case BlendFactor::OneMinusDstAlpha:
      return MTLBlendFactorOneMinusDestinationAlpha;
    case BlendFactor::Src1:
      return MTLBlendFactorSource1Color;
    case BlendFactor::OneMinusSrc1:
      return MTLBlendFactorOneMinusSource1Color;
    case BlendFactor::Src1Alpha:
      return MTLBlendFactorSource1Alpha;
    case BlendFactor::OneMinusSrc1Alpha:
      return MTLBlendFactorOneMinusSource1Alpha;
    default:
      return MTLBlendFactorOne;
  }
}

MTLBlendOperation MtlDefines::ToMTLBlendOperation(BlendOperation blendOp) {
  switch (blendOp) {
    case BlendOperation::Add:
      return MTLBlendOperationAdd;
    case BlendOperation::Subtract:
      return MTLBlendOperationSubtract;
    case BlendOperation::ReverseSubtract:
      return MTLBlendOperationReverseSubtract;
    case BlendOperation::Min:
      return MTLBlendOperationMin;
    case BlendOperation::Max:
      return MTLBlendOperationMax;
    default:
      return MTLBlendOperationAdd;
  }
}

MTLSamplerAddressMode MtlDefines::ToMTLSamplerAddressMode(AddressMode addressMode) {
  switch (addressMode) {
    case AddressMode::ClampToEdge:
      return MTLSamplerAddressModeClampToEdge;
    case AddressMode::Repeat:
      return MTLSamplerAddressModeRepeat;
    case AddressMode::MirrorRepeat:
      return MTLSamplerAddressModeMirrorRepeat;
    case AddressMode::ClampToBorder:
      return MTLSamplerAddressModeClampToBorderColor;
    default:
      return MTLSamplerAddressModeClampToEdge;
  }
}

MTLSamplerMinMagFilter MtlDefines::ToMTLSamplerFilter(FilterMode filter) {
  switch (filter) {
    case FilterMode::Nearest:
      return MTLSamplerMinMagFilterNearest;
    case FilterMode::Linear:
      return MTLSamplerMinMagFilterLinear;
    default:
      return MTLSamplerMinMagFilterLinear;
  }
}

MTLSamplerMipFilter MtlDefines::ToMTLSamplerMipFilter(MipmapMode filter) {
  switch (filter) {
    case MipmapMode::None:
      return MTLSamplerMipFilterNotMipmapped;
    case MipmapMode::Nearest:
      return MTLSamplerMipFilterNearest;
    case MipmapMode::Linear:
      return MTLSamplerMipFilterLinear;
    default:
      return MTLSamplerMipFilterNotMipmapped;
  }
}

size_t MtlDefines::GetBytesPerPixel(MTLPixelFormat format) {
  switch (format) {
    case MTLPixelFormatR8Unorm:
    case MTLPixelFormatR8Snorm:
    case MTLPixelFormatR8Uint:
    case MTLPixelFormatR8Sint:
      return 1;
    
    case MTLPixelFormatR16Unorm:
    case MTLPixelFormatR16Snorm:
    case MTLPixelFormatR16Uint:
    case MTLPixelFormatR16Sint:
    case MTLPixelFormatR16Float:
    case MTLPixelFormatRG8Unorm:
    case MTLPixelFormatRG8Snorm:
    case MTLPixelFormatRG8Uint:
    case MTLPixelFormatRG8Sint:
    case MTLPixelFormatB5G6R5Unorm:
    case MTLPixelFormatA1BGR5Unorm:
    case MTLPixelFormatABGR4Unorm:
      return 2;
    
    case MTLPixelFormatRGBA8Unorm:
    case MTLPixelFormatRGBA8Unorm_sRGB:
    case MTLPixelFormatRGBA8Snorm:
    case MTLPixelFormatRGBA8Uint:
    case MTLPixelFormatRGBA8Sint:
    case MTLPixelFormatBGRA8Unorm:
    case MTLPixelFormatBGRA8Unorm_sRGB:
    case MTLPixelFormatRGB10A2Unorm:
    case MTLPixelFormatRGB10A2Uint:
    case MTLPixelFormatRG16Unorm:
    case MTLPixelFormatRG16Snorm:
    case MTLPixelFormatRG16Uint:
    case MTLPixelFormatRG16Sint:
    case MTLPixelFormatRG16Float:
    case MTLPixelFormatR32Uint:
    case MTLPixelFormatR32Sint:
    case MTLPixelFormatR32Float:
      return 4;
    
    case MTLPixelFormatRGBA16Unorm:
    case MTLPixelFormatRGBA16Snorm:
    case MTLPixelFormatRGBA16Uint:
    case MTLPixelFormatRGBA16Sint:
    case MTLPixelFormatRGBA16Float:
    case MTLPixelFormatRG32Uint:
    case MTLPixelFormatRG32Sint:
    case MTLPixelFormatRG32Float:
      return 8;
    
    case MTLPixelFormatRGBA32Uint:
    case MTLPixelFormatRGBA32Sint:
    case MTLPixelFormatRGBA32Float:
      return 16;
    
    default:
      return 4; // Default to 4 bytes per pixel
  }
}

bool MtlDefines::IsCompressedFormat(MTLPixelFormat format) {
  // Metal compressed formats
  switch (format) {
    case MTLPixelFormatBC1_RGBA:
    case MTLPixelFormatBC1_RGBA_sRGB:
    case MTLPixelFormatBC2_RGBA:
    case MTLPixelFormatBC2_RGBA_sRGB:
    case MTLPixelFormatBC3_RGBA:
    case MTLPixelFormatBC3_RGBA_sRGB:
    case MTLPixelFormatBC4_RUnorm:
    case MTLPixelFormatBC4_RSnorm:
    case MTLPixelFormatBC5_RGUnorm:
    case MTLPixelFormatBC5_RGSnorm:
    case MTLPixelFormatBC6H_RGBFloat:
    case MTLPixelFormatBC6H_RGBUfloat:
    case MTLPixelFormatBC7_RGBAUnorm:
    case MTLPixelFormatBC7_RGBAUnorm_sRGB:
      return true;
    default:
      return false;
  }
}

bool MtlDefines::IsRenderableFormat(MTLPixelFormat format) {
  // Check if the format can be used as a render target
  switch (format) {
    case MTLPixelFormatRGBA8Unorm:
    case MTLPixelFormatRGBA8Unorm_sRGB:
    case MTLPixelFormatBGRA8Unorm:
    case MTLPixelFormatBGRA8Unorm_sRGB:
    case MTLPixelFormatRGB10A2Unorm:
    case MTLPixelFormatRGBA16Float:
    case MTLPixelFormatRGBA32Float:
    case MTLPixelFormatR8Unorm:
    case MTLPixelFormatRG8Unorm:
    case MTLPixelFormatR16Float:
    case MTLPixelFormatRG16Float:
    case MTLPixelFormatR32Float:
    case MTLPixelFormatRG32Float:
      return true;
    default:
      return false;
  }
}

MTLCullMode MtlDefines::ToMTLCullMode(CullMode cullMode) {
  switch (cullMode) {
    case CullMode::None:
      return MTLCullModeNone;
    case CullMode::Front:
      return MTLCullModeFront;
    case CullMode::Back:
      return MTLCullModeBack;
  }
  return MTLCullModeNone;
}

MTLWinding MtlDefines::ToMTLWinding(FrontFace frontFace) {
  switch (frontFace) {
    case FrontFace::CW:
      return MTLWindingClockwise;
    case FrontFace::CCW:
      return MTLWindingCounterClockwise;
  }
  return MTLWindingCounterClockwise;
}

}  // namespace tgfx