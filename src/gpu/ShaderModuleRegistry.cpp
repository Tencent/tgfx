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

static const std::string kRadialGradientLayout = R"GLSL(
tgfx_float4 FP_RadialGradientLayout(tgfx_float2 coord) {
    float t = length(coord);
    return tgfx_float4(t, 1.0, 0.0, 0.0);
}
)GLSL";

static const std::string kDiamondGradientLayout = R"GLSL(
tgfx_float4 FP_DiamondGradientLayout(tgfx_float2 coord) {
    float t = max(abs(coord.x), abs(coord.y));
    return tgfx_float4(t, 1.0, 0.0, 0.0);
}
)GLSL";

static const std::string kConicGradientLayout = R"GLSL(
tgfx_float4 FP_ConicGradientLayout(tgfx_float2 coord, float u_Bias, float u_Scale) {
    float angle = atan(-coord.y, -coord.x);
    float t = ((angle * 0.15915494309180001 + 0.5) + u_Bias) * u_Scale;
    return tgfx_float4(t, 1.0, 0.0, 0.0);
}
)GLSL";

static const std::string kAARectEffect = R"GLSL(
tgfx_float4 FP_AARectEffect(tgfx_float4 inputColor, tgfx_float4 u_Rect) {
    tgfx_float4 dists4 = clamp(tgfx_float4(1.0, 1.0, -1.0, -1.0) *
                                (gl_FragCoord.xyxy - u_Rect), 0.0, 1.0);
    tgfx_float2 dists2 = dists4.xy + dists4.zw - 1.0;
    float coverage = dists2.x * dists2.y;
    return inputColor * coverage;
}
)GLSL";

static const std::string kColorMatrix = R"GLSL(
tgfx_float4 FP_ColorMatrix(tgfx_float4 inputColor, mat4 u_Matrix, tgfx_float4 u_Vector) {
    tgfx_float4 unpremul = tgfx_float4(inputColor.rgb / max(inputColor.a, 9.9999997473787516e-05),
                                         inputColor.a);
    tgfx_float4 result = u_Matrix * unpremul + u_Vector;
    result = clamp(result, 0.0, 1.0);
    result.rgb *= result.a;
    return result;
}
)GLSL";

static const std::string kLuma = R"GLSL(
tgfx_float4 FP_Luma(tgfx_float4 inputColor, float u_Kr, float u_Kg, float u_Kb) {
    float luma = dot(inputColor.rgb, tgfx_float3(u_Kr, u_Kg, u_Kb));
    return tgfx_float4(luma);
}
)GLSL";

static const std::string kDualIntervalGradientColorizer = R"GLSL(
tgfx_float4 FP_DualIntervalGradientColorizer(tgfx_float4 inputColor,
    tgfx_float4 u_scale01, tgfx_float4 u_bias01,
    tgfx_float4 u_scale23, tgfx_float4 u_bias23, float u_threshold) {
    float t = inputColor.x;
    tgfx_float4 scale, bias;
    if (t < u_threshold) {
        scale = u_scale01;
        bias = u_bias01;
    } else {
        scale = u_scale23;
        bias = u_bias23;
    }
    return tgfx_float4(t * scale + bias);
}
)GLSL";

static const std::string kAlphaThreshold = R"GLSL(
tgfx_float4 FP_AlphaThreshold(tgfx_float4 inputColor, float u_Threshold) {
    tgfx_float4 result = tgfx_float4(0.0);
    if (inputColor.a > 0.0) {
        result.rgb = inputColor.rgb / inputColor.a;
        result.a = step(u_Threshold, inputColor.a);
        result = clamp(result, 0.0, 1.0);
    }
    return result;
}
)GLSL";

static const std::string kTextureGradientColorizer = R"GLSL(
tgfx_float4 FP_TextureGradientColorizer(tgfx_float4 inputColor, sampler2D u_Sampler) {
    tgfx_float2 coord = tgfx_float2(inputColor.x, 0.5);
    return texture(u_Sampler, coord);
}
)GLSL";

static const std::string kDeviceSpaceTextureEffect = R"GLSL(
tgfx_float4 FP_DeviceSpaceTextureEffect(tgfx_float4 inputColor, sampler2D u_Sampler,
                                          mat3 u_DeviceCoordMatrix) {
    tgfx_float3 deviceCoord = u_DeviceCoordMatrix * tgfx_float3(gl_FragCoord.xy, 1.0);
    tgfx_float4 color = texture(u_Sampler, deviceCoord.xy);
#if TGFX_DST_IS_ALPHA_ONLY
    return color.a * inputColor;
#else
    return color * color.a;
#endif
}
)GLSL";

// ---- Processor name -> module ID mapping ----

static const std::unordered_map<std::string, ShaderModuleID> kProcessorModuleMap = {
    {"ConstColorProcessor", ShaderModuleID::ConstColor},
    {"LinearGradientLayout", ShaderModuleID::LinearGradientLayout},
    {"SingleIntervalGradientColorizer", ShaderModuleID::SingleIntervalGradientColorizer},
    {"RadialGradientLayout", ShaderModuleID::RadialGradientLayout},
    {"DiamondGradientLayout", ShaderModuleID::DiamondGradientLayout},
    {"ConicGradientLayout", ShaderModuleID::ConicGradientLayout},
    {"AARectEffect", ShaderModuleID::AARectEffect},
    {"ColorMatrixFragmentProcessor", ShaderModuleID::ColorMatrix},
    {"LumaFragmentProcessor", ShaderModuleID::Luma},
    {"DualIntervalGradientColorizer", ShaderModuleID::DualIntervalGradientColorizer},
    {"AlphaThresholdFragmentProcessor", ShaderModuleID::AlphaThreshold},
    {"TextureGradientColorizer", ShaderModuleID::TextureGradientColorizer},
    {"DeviceSpaceTextureEffect", ShaderModuleID::DeviceSpaceTextureEffect},
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
    case ShaderModuleID::RadialGradientLayout:
      return kRadialGradientLayout;
    case ShaderModuleID::DiamondGradientLayout:
      return kDiamondGradientLayout;
    case ShaderModuleID::ConicGradientLayout:
      return kConicGradientLayout;
    case ShaderModuleID::AARectEffect:
      return kAARectEffect;
    case ShaderModuleID::ColorMatrix:
      return kColorMatrix;
    case ShaderModuleID::Luma:
      return kLuma;
    case ShaderModuleID::DualIntervalGradientColorizer:
      return kDualIntervalGradientColorizer;
    case ShaderModuleID::AlphaThreshold:
      return kAlphaThreshold;
    case ShaderModuleID::TextureGradientColorizer:
      return kTextureGradientColorizer;
    case ShaderModuleID::DeviceSpaceTextureEffect:
      return kDeviceSpaceTextureEffect;
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
