/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "ShaderModuleRegistry.h"
#include <unordered_map>

namespace tgfx {

// ---- Embedded shader module sources (from src/gpu/shaders/) ----

static const std::string kTypesGLSL = R"GLSL(
// tgfx_types_glsl.glsl - GLSL backend type definitions.
#define tgfx_float    float
#define tgfx_float2   vec2
#define tgfx_float3   vec3
#define tgfx_float4   vec4
#define tgfx_float2x2 mat2
#define tgfx_float3x3 mat3
#define tgfx_float4x4 mat4
#define tgfx_int      int
#define tgfx_int2     ivec2
#define tgfx_int3     ivec3
#define tgfx_int4     ivec4
#define tgfx_half     float
#define tgfx_half2    vec2
#define tgfx_half3    vec3
#define tgfx_half4    vec4
#define tgfx_sample(sampler, coord) texture(sampler, coord)
)GLSL";

static const std::string kConstColor = R"GLSL(
tgfx_float4 FP_ConstColor(tgfx_float4 inputColor, tgfx_float4 u_Color) {
    tgfx_float4 result = u_Color;
#if TGFX_CONST_COLOR_INPUT_MODE == 1
    result *= inputColor;
#elif TGFX_CONST_COLOR_INPUT_MODE == 2
    result *= inputColor.a;
#endif
    return result;
}
)GLSL";

static const std::string kLinearGradientLayout = R"GLSL(
tgfx_float4 FP_LinearGradientLayout(tgfx_float2 coord) {
    float t = coord.x + 1.0000000000000001e-05;
    return tgfx_float4(t, 1.0, 0.0, 0.0);
}
)GLSL";

static const std::string kSingleIntervalGradientColorizer = R"GLSL(
tgfx_float4 FP_SingleIntervalGradientColorizer(tgfx_float4 inputColor,
                                                 tgfx_float4 u_start,
                                                 tgfx_float4 u_end) {
    float t = inputColor.x;
    return (1.0 - t) * u_start + t * u_end;
}
)GLSL";

// ---- Processor name -> module ID mapping ----

static const std::unordered_map<std::string, ShaderModuleID> kProcessorModuleMap = {
    {"ConstColorProcessor", ShaderModuleID::ConstColor},
    {"LinearGradientLayout", ShaderModuleID::LinearGradientLayout},
    {"SingleIntervalGradientColorizer", ShaderModuleID::SingleIntervalGradientColorizer},
};

// ---- Public API implementation ----

const std::string& ShaderModuleRegistry::GetModule(ShaderModuleID id) {
  switch (id) {
    case ShaderModuleID::TypesGLSL:
      return kTypesGLSL;
    case ShaderModuleID::ConstColor:
      return kConstColor;
    case ShaderModuleID::LinearGradientLayout:
      return kLinearGradientLayout;
    case ShaderModuleID::SingleIntervalGradientColorizer:
      return kSingleIntervalGradientColorizer;
  }
  static const std::string kEmpty;
  return kEmpty;
}

bool ShaderModuleRegistry::HasModule(const std::string& processorName) {
  return kProcessorModuleMap.count(processorName) > 0;
}

ShaderModuleID ShaderModuleRegistry::GetModuleID(const std::string& processorName) {
  return kProcessorModuleMap.at(processorName);
}

}  // namespace tgfx
