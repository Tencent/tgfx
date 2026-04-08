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
// Copyright (C) 2026 Tencent. All rights reserved.
// const_color.glsl - ConstColorProcessor modular shader function.
// Compile switch: TGFX_CC_MODE
//   0 = Ignore  (output = color)
//   1 = ModulateRGBA (output = color * inputColor)
//   2 = ModulateA (output = color * inputColor.a)

vec4 TGFX_ConstColor(vec4 inputColor, vec4 color) {
    vec4 result = color;
#if TGFX_CC_MODE == 1
    result *= inputColor;
#elif TGFX_CC_MODE == 2
    result *= inputColor.a;
#endif
    return result;
}
)GLSL";

static const std::string kLinearGradientLayout = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// linear_gradient_layout.glsl - LinearGradientLayout modular shader function.
// Takes a transformed coordinate and outputs the gradient t-value as vec4(t, 1.0, 0.0, 0.0).

vec4 TGFX_LinearGradientLayout(vec2 coord) {
    float t = coord.x + 1.0000000000000001e-05;
    return vec4(t, 1.0, 0.0, 0.0);
}
)GLSL";

static const std::string kSingleIntervalGradientColorizer = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// single_interval_gradient_colorizer.glsl - SingleIntervalGradientColorizer modular shader function.
// Performs linear interpolation between start and end based on inputColor.x (the t-value).

vec4 TGFX_SingleIntervalGradientColorizer(vec4 inputColor, vec4 start, vec4 end) {
    float t = inputColor.x;
    return (1.0 - t) * start + t * end;
}
)GLSL";

static const std::string kRadialGradientLayout = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// radial_gradient_layout.glsl - RadialGradientLayout modular shader function.
// Takes a transformed coordinate and outputs the gradient t-value as vec4(t, 1.0, 0.0, 0.0).

vec4 TGFX_RadialGradientLayout(vec2 coord) {
    float t = length(coord);
    return vec4(t, 1.0, 0.0, 0.0);
}
)GLSL";

static const std::string kDiamondGradientLayout = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// diamond_gradient_layout.glsl - DiamondGradientLayout modular shader function.
// Takes a transformed coordinate and outputs the gradient t-value as vec4(t, 1.0, 0.0, 0.0).

vec4 TGFX_DiamondGradientLayout(vec2 coord) {
    float t = max(abs(coord.x), abs(coord.y));
    return vec4(t, 1.0, 0.0, 0.0);
}
)GLSL";

static const std::string kConicGradientLayout = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// conic_gradient_layout.glsl - ConicGradientLayout modular shader function.
// Takes a transformed coordinate + bias/scale uniforms, outputs gradient t-value.

vec4 TGFX_ConicGradientLayout(vec2 coord, float bias, float scale) {
    float angle = atan(-coord.y, -coord.x);
    float t = ((angle * 0.15915494309180001 + 0.5) + bias) * scale;
    return vec4(t, 1.0, 0.0, 0.0);
}
)GLSL";

static const std::string kAARectEffect = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// aa_rect_effect.glsl - AARectEffect modular shader function.
// Computes anti-aliased rectangle coverage using gl_FragCoord.
// rect = vec4(left, top, right, bottom) outset by 0.5 for AA math.

vec4 TGFX_AARectEffect(vec4 inputColor, vec4 rect) {
    vec4 dists4 = clamp(vec4(1.0, 1.0, -1.0, -1.0) *
                        (gl_FragCoord.xyxy - rect), 0.0, 1.0);
    vec2 dists2 = dists4.xy + dists4.zw - 1.0;
    float coverage = dists2.x * dists2.y;
    return inputColor * coverage;
}
)GLSL";

static const std::string kColorMatrix = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// color_matrix.glsl - ColorMatrixFragmentProcessor modular shader function.
// Applies a 4x4 color matrix + offset vector to the input color.
// Input is unpremultiplied before matrix multiply, then re-premultiplied.

vec4 TGFX_ColorMatrix(vec4 inputColor, mat4 matrix, vec4 vector) {
    vec4 unpremul = vec4(inputColor.rgb / max(inputColor.a, 9.9999997473787516e-05),
                         inputColor.a);
    vec4 result = matrix * unpremul + vector;
    result = clamp(result, 0.0, 1.0);
    result.rgb *= result.a;
    return result;
}
)GLSL";

static const std::string kLuma = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// luma.glsl - LumaFragmentProcessor modular shader function.
// Converts input color to luminance using configurable coefficients.

vec4 TGFX_Luma(vec4 inputColor, float kr, float kg, float kb) {
    float luma = dot(inputColor.rgb, vec3(kr, kg, kb));
    return vec4(luma);
}
)GLSL";

static const std::string kDualIntervalGradientColorizer = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// dual_interval_gradient_colorizer.glsl - DualIntervalGradientColorizer modular shader function.
// Performs piecewise linear interpolation across two intervals split by a threshold.

vec4 TGFX_DualIntervalGradientColorizer(vec4 inputColor,
    vec4 scale01, vec4 bias01,
    vec4 scale23, vec4 bias23, float threshold) {
    float t = inputColor.x;
    vec4 scale, bias;
    if (t < threshold) {
        scale = scale01;
        bias = bias01;
    } else {
        scale = scale23;
        bias = bias23;
    }
    return vec4(t * scale + bias);
}
)GLSL";

static const std::string kAlphaThreshold = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// alpha_threshold.glsl - AlphaThresholdFragmentProcessor modular shader function.
// Applies binary alpha thresholding: alpha becomes 0 or 1 based on threshold comparison.

vec4 TGFX_AlphaThreshold(vec4 inputColor, float threshold) {
    vec4 result = vec4(0.0);
    if (inputColor.a > 0.0) {
        result.rgb = inputColor.rgb / inputColor.a;
        result.a = step(threshold, inputColor.a);
        result = clamp(result, 0.0, 1.0);
    }
    return result;
}
)GLSL";

static const std::string kTextureGradientColorizer = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// texture_gradient_colorizer.glsl - TextureGradientColorizer modular shader function.
// Samples a 1D gradient texture using the t-value from inputColor.x.

vec4 TGFX_TextureGradientColorizer(vec4 inputColor, sampler2D gradientSampler) {
    vec2 coord = vec2(inputColor.x, 0.5);
    return texture(gradientSampler, coord);
}
)GLSL";

static const std::string kDeviceSpaceTextureEffect = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// device_space_texture_effect.glsl - DeviceSpaceTextureEffect modular shader function.
// Samples a texture using device-space coordinates (gl_FragCoord) transformed by a matrix.
// Compile switch: TGFX_DSTE_ALPHA_ONLY
//   0 = output premultiplied color (color * color.a)
//   1 = output alpha modulated by inputColor (color.a * inputColor)

vec4 TGFX_DeviceSpaceTextureEffect(vec4 inputColor, sampler2D textureSampler,
                                    mat3 deviceCoordMatrix) {
    vec3 deviceCoord = deviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
    vec4 color = texture(textureSampler, deviceCoord.xy);
#if TGFX_DSTE_ALPHA_ONLY
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
