////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/SLType.h"
#include "gpu/ir/ProgramIR.h"
#include "gpu/shaders/contract/ShaderLibraryBundleFormat.h"

namespace tgfx {

/// The language's spelling of a value type. Null for the types a body cannot declare (samplers,
/// void): the statement printer treats those as "this program is not printable here".
inline const char* ValueTypeName(ShaderLibraryLanguage language, SLType type) {
  const bool msl = language == ShaderLibraryLanguage::MSL;
  const bool wgsl = language == ShaderLibraryLanguage::WGSL;
  switch (type) {
    case SLType::Float:
      return wgsl ? "f32" : "float";
    case SLType::Float2:
      return wgsl ? "vec2<f32>" : (msl ? "float2" : "vec2");
    case SLType::Float3:
      return wgsl ? "vec3<f32>" : (msl ? "float3" : "vec3");
    case SLType::Float4:
      return wgsl ? "vec4<f32>" : (msl ? "float4" : "vec4");
    case SLType::Int:
      return wgsl ? "i32" : "int";
    case SLType::Int2:
      return wgsl ? "vec2<i32>" : (msl ? "int2" : "ivec2");
    case SLType::Int3:
      return wgsl ? "vec3<i32>" : (msl ? "int3" : "ivec3");
    case SLType::Int4:
      return wgsl ? "vec4<i32>" : (msl ? "int4" : "ivec4");
    case SLType::Float2x2:
      return wgsl ? "mat2x2<f32>" : (msl ? "float2x2" : "mat2");
    case SLType::Float3x3:
      return wgsl ? "mat3x3<f32>" : (msl ? "float3x3" : "mat3");
    case SLType::Float4x4:
      return wgsl ? "mat4x4<f32>" : (msl ? "float4x4" : "mat4");
    default:
      return nullptr;
  }
}

/// The declaration's precision prefix. Only GLSL carries one; MSL's precision is the type itself.
inline const char* ValuePrecisionPrefix(ShaderLibraryLanguage language, Precision precision) {
  if (language != ShaderLibraryLanguage::GLSL) {
    return "";
  }
  switch (precision) {
    case Precision::High:
      return "highp ";
    case Precision::Medium:
      return "mediump ";
    default:
      return "";
  }
}

}  // namespace tgfx
