/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "ShaderVar.h"

namespace tgfx {
static SLType GetSLType(VertexFormat format) {
  switch (format) {
    case VertexFormat::Float:
      return SLType::Float;
    case VertexFormat::Float2:
      return SLType::Float2;
    case VertexFormat::Float3:
      return SLType::Float3;
    case VertexFormat::Float4:
      return SLType::Float4;
    case VertexFormat::Half:
      return SLType::Half;
    case VertexFormat::Half2:
      return SLType::Half2;
    case VertexFormat::Half3:
      return SLType::Half3;
    case VertexFormat::Half4:
      return SLType::Half4;
    case VertexFormat::Int:
      return SLType::Int;
    case VertexFormat::Int2:
      return SLType::Int2;
    case VertexFormat::Int3:
      return SLType::Int3;
    case VertexFormat::Int4:
      return SLType::Int4;
    case VertexFormat::UByteNormalized:
      return SLType::Float;
    case VertexFormat::UByte2Normalized:
      return SLType::Float2;
    case VertexFormat::UByte3Normalized:
      return SLType::Float3;
    case VertexFormat::UByte4Normalized:
      return SLType::Float4;
  }
  return SLType::Void;
}

static SLType GetSLType(UniformFormat format) {
  switch (format) {
    case UniformFormat::Float:
      return SLType::Float;
    case UniformFormat::Float2:
      return SLType::Float2;
    case UniformFormat::Float3:
      return SLType::Float3;
    case UniformFormat::Float4:
      return SLType::Float4;
    case UniformFormat::Float2x2:
      return SLType::Float2x2;
    case UniformFormat::Float3x3:
      return SLType::Float3x3;
    case UniformFormat::Float4x4:
      return SLType::Float4x4;
    case UniformFormat::Int:
      return SLType::Int;
    case UniformFormat::Int2:
      return SLType::Int2;
    case UniformFormat::Int3:
      return SLType::Int3;
    case UniformFormat::Int4:
      return SLType::Int4;
    case UniformFormat::Texture2DSampler:
      return SLType::Texture2DSampler;
    case UniformFormat::TextureExternalSampler:
      return SLType::TextureExternalSampler;
    case UniformFormat::Texture2DRectSampler:
      return SLType::Texture2DRectSampler;
  }
  return SLType::Void;
}

ShaderVar::ShaderVar(const Attribute& attribute)
    : _name(attribute.name()), _type(GetSLType(attribute.format())),
      _modifier(TypeModifier::Attribute) {
}

ShaderVar::ShaderVar(const Uniform& uniform)
    : _name(uniform.name()), _type(GetSLType(uniform.format())), _modifier(TypeModifier::Uniform) {
}

}  // namespace tgfx
