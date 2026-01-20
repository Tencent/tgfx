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
#include "tgfx/gpu/Texture.h"

namespace tgfx {

wgpu::TextureFormat ToWGPUTextureFormat(PixelFormat pixelFormat) {
  switch (pixelFormat) {
    case PixelFormat::ALPHA_8:
    case PixelFormat::GRAY_8:
      return wgpu::TextureFormat::R8Unorm;
    case PixelFormat::RG_88:
      return wgpu::TextureFormat::RG8Unorm;
    case PixelFormat::RGBA_8888:
      return wgpu::TextureFormat::RGBA8Unorm;
    case PixelFormat::BGRA_8888:
      return wgpu::TextureFormat::BGRA8Unorm;
    case PixelFormat::DEPTH24_STENCIL8:
      return wgpu::TextureFormat::Depth24PlusStencil8;
    default:
      return wgpu::TextureFormat::Undefined;
  }
}

wgpu::TextureUsage ToWGPUTextureUsage(uint32_t usage) {
  wgpu::TextureUsage wgpuUsage = wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc;
  if (usage & TextureUsage::TEXTURE_BINDING) {
    wgpuUsage |= wgpu::TextureUsage::TextureBinding;
  }
  if (usage & TextureUsage::RENDER_ATTACHMENT) {
    wgpuUsage |= wgpu::TextureUsage::RenderAttachment;
  }
  return wgpuUsage;
}

wgpu::AddressMode ToWGPUAddressMode(AddressMode mode) {
  switch (mode) {
    case AddressMode::ClampToEdge:
      return wgpu::AddressMode::ClampToEdge;
    case AddressMode::Repeat:
      return wgpu::AddressMode::Repeat;
    case AddressMode::MirrorRepeat:
      return wgpu::AddressMode::MirrorRepeat;
    case AddressMode::ClampToBorder:
      // WebGPU doesn't support ClampToBorder directly, fallback to ClampToEdge.
      return wgpu::AddressMode::ClampToEdge;
  }
  return wgpu::AddressMode::ClampToEdge;
}

wgpu::FilterMode ToWGPUFilterMode(FilterMode mode) {
  switch (mode) {
    case FilterMode::Nearest:
      return wgpu::FilterMode::Nearest;
    case FilterMode::Linear:
      return wgpu::FilterMode::Linear;
  }
  return wgpu::FilterMode::Nearest;
}

wgpu::MipmapFilterMode ToWGPUMipmapFilterMode(MipmapMode mode) {
  switch (mode) {
    case MipmapMode::None:
    case MipmapMode::Nearest:
      return wgpu::MipmapFilterMode::Nearest;
    case MipmapMode::Linear:
      return wgpu::MipmapFilterMode::Linear;
  }
  return wgpu::MipmapFilterMode::Nearest;
}

wgpu::VertexFormat ToWGPUVertexFormat(VertexFormat format) {
  switch (format) {
    case VertexFormat::Float:
      return wgpu::VertexFormat::Float32;
    case VertexFormat::Float2:
      return wgpu::VertexFormat::Float32x2;
    case VertexFormat::Float3:
      return wgpu::VertexFormat::Float32x3;
    case VertexFormat::Float4:
      return wgpu::VertexFormat::Float32x4;
    case VertexFormat::Half:
      return wgpu::VertexFormat::Float16x2;  // WebGPU doesn't have Float16, use Float16x2
    case VertexFormat::Half2:
      return wgpu::VertexFormat::Float16x2;
    case VertexFormat::Half3:
      return wgpu::VertexFormat::Float16x4;  // WebGPU doesn't have Float16x3, use Float16x4
    case VertexFormat::Half4:
      return wgpu::VertexFormat::Float16x4;
    case VertexFormat::Int:
      return wgpu::VertexFormat::Sint32;
    case VertexFormat::Int2:
      return wgpu::VertexFormat::Sint32x2;
    case VertexFormat::Int3:
      return wgpu::VertexFormat::Sint32x3;
    case VertexFormat::Int4:
      return wgpu::VertexFormat::Sint32x4;
    case VertexFormat::UByteNormalized:
      return wgpu::VertexFormat::Unorm8x2;  // WebGPU doesn't have Unorm8, use Unorm8x2
    case VertexFormat::UByte2Normalized:
      return wgpu::VertexFormat::Unorm8x2;
    case VertexFormat::UByte3Normalized:
      return wgpu::VertexFormat::Unorm8x4;  // WebGPU doesn't have Unorm8x3, use Unorm8x4
    case VertexFormat::UByte4Normalized:
      return wgpu::VertexFormat::Unorm8x4;
  }
  return wgpu::VertexFormat::Float32;
}

wgpu::BlendFactor ToWGPUBlendFactor(BlendFactor factor) {
  switch (factor) {
    case BlendFactor::Zero:
      return wgpu::BlendFactor::Zero;
    case BlendFactor::One:
      return wgpu::BlendFactor::One;
    case BlendFactor::Src:
      return wgpu::BlendFactor::Src;
    case BlendFactor::OneMinusSrc:
      return wgpu::BlendFactor::OneMinusSrc;
    case BlendFactor::Dst:
      return wgpu::BlendFactor::Dst;
    case BlendFactor::OneMinusDst:
      return wgpu::BlendFactor::OneMinusDst;
    case BlendFactor::SrcAlpha:
      return wgpu::BlendFactor::SrcAlpha;
    case BlendFactor::OneMinusSrcAlpha:
      return wgpu::BlendFactor::OneMinusSrcAlpha;
    case BlendFactor::DstAlpha:
      return wgpu::BlendFactor::DstAlpha;
    case BlendFactor::OneMinusDstAlpha:
      return wgpu::BlendFactor::OneMinusDstAlpha;
    case BlendFactor::Src1:
      // WebGPU doesn't support dual-source blending, fallback to Src.
      return wgpu::BlendFactor::Src;
    case BlendFactor::OneMinusSrc1:
      // WebGPU doesn't support dual-source blending, fallback to OneMinusSrc.
      return wgpu::BlendFactor::OneMinusSrc;
    case BlendFactor::Src1Alpha:
      // WebGPU doesn't support dual-source blending, fallback to SrcAlpha.
      return wgpu::BlendFactor::SrcAlpha;
    case BlendFactor::OneMinusSrc1Alpha:
      // WebGPU doesn't support dual-source blending, fallback to OneMinusSrcAlpha.
      return wgpu::BlendFactor::OneMinusSrcAlpha;
  }
  return wgpu::BlendFactor::One;
}

wgpu::BlendOperation ToWGPUBlendOperation(BlendOperation op) {
  switch (op) {
    case BlendOperation::Add:
      return wgpu::BlendOperation::Add;
    case BlendOperation::Subtract:
      return wgpu::BlendOperation::Subtract;
    case BlendOperation::ReverseSubtract:
      return wgpu::BlendOperation::ReverseSubtract;
    case BlendOperation::Min:
      return wgpu::BlendOperation::Min;
    case BlendOperation::Max:
      return wgpu::BlendOperation::Max;
  }
  return wgpu::BlendOperation::Add;
}

wgpu::CompareFunction ToWGPUCompareFunction(CompareFunction func) {
  switch (func) {
    case CompareFunction::Never:
      return wgpu::CompareFunction::Never;
    case CompareFunction::Less:
      return wgpu::CompareFunction::Less;
    case CompareFunction::Equal:
      return wgpu::CompareFunction::Equal;
    case CompareFunction::LessEqual:
      return wgpu::CompareFunction::LessEqual;
    case CompareFunction::Greater:
      return wgpu::CompareFunction::Greater;
    case CompareFunction::NotEqual:
      return wgpu::CompareFunction::NotEqual;
    case CompareFunction::GreaterEqual:
      return wgpu::CompareFunction::GreaterEqual;
    case CompareFunction::Always:
      return wgpu::CompareFunction::Always;
  }
  return wgpu::CompareFunction::Always;
}

wgpu::StencilOperation ToWGPUStencilOperation(StencilOperation op) {
  switch (op) {
    case StencilOperation::Keep:
      return wgpu::StencilOperation::Keep;
    case StencilOperation::Zero:
      return wgpu::StencilOperation::Zero;
    case StencilOperation::Replace:
      return wgpu::StencilOperation::Replace;
    case StencilOperation::Invert:
      return wgpu::StencilOperation::Invert;
    case StencilOperation::IncrementClamp:
      return wgpu::StencilOperation::IncrementClamp;
    case StencilOperation::DecrementClamp:
      return wgpu::StencilOperation::DecrementClamp;
    case StencilOperation::IncrementWrap:
      return wgpu::StencilOperation::IncrementWrap;
    case StencilOperation::DecrementWrap:
      return wgpu::StencilOperation::DecrementWrap;
  }
  return wgpu::StencilOperation::Keep;
}

wgpu::CullMode ToWGPUCullMode(CullMode mode) {
  switch (mode) {
    case CullMode::None:
      return wgpu::CullMode::None;
    case CullMode::Front:
      return wgpu::CullMode::Front;
    case CullMode::Back:
      return wgpu::CullMode::Back;
  }
  return wgpu::CullMode::None;
}

wgpu::FrontFace ToWGPUFrontFace(FrontFace face) {
  switch (face) {
    case FrontFace::CW:
      return wgpu::FrontFace::CW;
    case FrontFace::CCW:
      return wgpu::FrontFace::CCW;
  }
  return wgpu::FrontFace::CCW;
}

}  // namespace tgfx
