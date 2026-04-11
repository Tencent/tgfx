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
// mode: 0 = Ignore (output = color), 1 = ModulateRGBA, 2 = ModulateA

vec4 TGFX_ConstColor(vec4 inputColor, vec4 color, int mode) {
    vec4 result = color;
    if (mode == 1) {
        result *= inputColor;
    } else if (mode == 2) {
        result *= inputColor.a;
    }
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
//   1 = output alpha modulated by inputColor (color.r * inputColor)

vec4 TGFX_DeviceSpaceTextureEffect(vec4 inputColor, sampler2D textureSampler,
                                    mat3 deviceCoordMatrix) {
    vec3 deviceCoord = deviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
    vec4 color = texture(textureSampler, deviceCoord.xy);
#if TGFX_DSTE_ALPHA_ONLY
    return color.r * inputColor;
#else
    return color * color.a;
#endif
}
)GLSL";

static const std::string kTextureEffect = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// texture_effect.frag.glsl - TextureEffect modular shader function.
// Samples a texture with optional subset clamping, YUV conversion, and RGBAAA alpha channel.
//
// Required macros:
//   TGFX_TE_TEXTURE_MODE - 0=RGBA, 1=I420, 2=NV12
//
// Optional macros:
//   TGFX_TE_SUBSET          - defined if subset clamping is needed
//   TGFX_TE_STRICT_CONSTRAINT - defined if extra strict subset clamp is needed
//   TGFX_TE_ALPHA_ONLY      - defined if texture is alpha-only
//   TGFX_TE_RGBAAA          - defined if RGBAAA alpha channel layout is used
//   TGFX_TE_YUV_LIMITED_RANGE - defined if YUV uses limited range (16-235)

#ifndef TGFX_TE_TEXTURE_MODE
#define TGFX_TE_TEXTURE_MODE 0
#endif

#ifndef TGFX_TE_SAMPLER_TYPE
#define TGFX_TE_SAMPLER_TYPE sampler2D
#endif

vec2 TGFX_TE_ClampCoord(vec2 coord
#ifdef TGFX_TE_STRICT_CONSTRAINT
    , vec4 extraSubset
#endif
#ifdef TGFX_TE_SUBSET
    , vec4 subset
#endif
) {
    vec2 result = coord;
#ifdef TGFX_TE_STRICT_CONSTRAINT
    result = clamp(result, extraSubset.xy, extraSubset.zw);
#endif
#ifdef TGFX_TE_SUBSET
    result = clamp(result, subset.xy, subset.zw);
#endif
    return result;
}

#if TGFX_TE_TEXTURE_MODE == 0

// RGBA path
vec4 TGFX_TextureEffect(vec4 inputColor, vec2 texCoord, TGFX_TE_SAMPLER_TYPE textureSampler
#ifdef TGFX_TE_SUBSET
    , vec4 subset
#endif
#ifdef TGFX_TE_STRICT_CONSTRAINT
    , vec4 extraSubset
#endif
#ifdef TGFX_TE_RGBAAA
    , vec2 alphaStart
#endif
) {
    vec2 finalCoord = TGFX_TE_ClampCoord(texCoord
#ifdef TGFX_TE_STRICT_CONSTRAINT
        , extraSubset
#endif
#ifdef TGFX_TE_SUBSET
        , subset
#endif
    );
    vec4 color = texture(textureSampler, finalCoord);
#ifdef TGFX_TE_ALPHA_ONLY
    // Alpha-only textures (R8 format) store alpha in the red channel.
    color = color.rrrr;
#endif
#ifdef TGFX_TE_RGBAAA
    color = clamp(color, 0.0, 1.0);
    vec2 alphaVertexColor = finalCoord + alphaStart;
    vec4 alpha = texture(textureSampler, alphaVertexColor);
    alpha = clamp(alpha, 0.0, 1.0);
    color = vec4(color.rgb * alpha.r, alpha.r);
#endif
#ifdef TGFX_TE_ALPHA_ONLY
    return color.a * inputColor;
#else
    return color * inputColor.a;
#endif
}

#elif TGFX_TE_TEXTURE_MODE == 1

// I420 YUV path - 3 separate planes (Y, U, V)
vec4 TGFX_TextureEffect(vec4 inputColor, vec2 texCoord,
    TGFX_TE_SAMPLER_TYPE samplerY, TGFX_TE_SAMPLER_TYPE samplerU, TGFX_TE_SAMPLER_TYPE samplerV,
    mat3 colorConversion
#ifdef TGFX_TE_SUBSET
    , vec4 subset
#endif
#ifdef TGFX_TE_STRICT_CONSTRAINT
    , vec4 extraSubset
#endif
#ifdef TGFX_TE_RGBAAA
    , vec2 alphaStart
#endif
) {
    vec2 finalCoord = TGFX_TE_ClampCoord(texCoord
#ifdef TGFX_TE_STRICT_CONSTRAINT
        , extraSubset
#endif
#ifdef TGFX_TE_SUBSET
        , subset
#endif
    );
    vec3 yuv;
    yuv.x = texture(samplerY, finalCoord).r;
    finalCoord = TGFX_TE_ClampCoord(texCoord
#ifdef TGFX_TE_STRICT_CONSTRAINT
        , extraSubset
#endif
#ifdef TGFX_TE_SUBSET
        , subset
#endif
    );
    yuv.y = texture(samplerU, finalCoord).r;
    finalCoord = TGFX_TE_ClampCoord(texCoord
#ifdef TGFX_TE_STRICT_CONSTRAINT
        , extraSubset
#endif
#ifdef TGFX_TE_SUBSET
        , subset
#endif
    );
    yuv.z = texture(samplerV, finalCoord).r;
#ifdef TGFX_TE_YUV_LIMITED_RANGE
    yuv.x -= (16.0 / 255.0);
#endif
    yuv.yz -= vec2(0.5, 0.5);
    vec3 rgb = clamp(colorConversion * yuv, 0.0, 1.0);
#ifdef TGFX_TE_RGBAAA
    vec2 alphaVertexColor = finalCoord + alphaStart;
    float yuv_a = texture(samplerY, alphaVertexColor).r;
    yuv_a = (yuv_a - 16.0 / 255.0) / (219.0 / 255.0 - 1.0 / 255.0);
    yuv_a = clamp(yuv_a, 0.0, 1.0);
    return vec4(rgb * yuv_a, yuv_a);
#else
    return vec4(rgb, 1.0);
#endif
}

#elif TGFX_TE_TEXTURE_MODE == 2

// NV12 YUV path - 2 planes (Y, UV interleaved)
vec4 TGFX_TextureEffect(vec4 inputColor, vec2 texCoord,
    TGFX_TE_SAMPLER_TYPE samplerY, TGFX_TE_SAMPLER_TYPE samplerUV,
    mat3 colorConversion
#ifdef TGFX_TE_SUBSET
    , vec4 subset
#endif
#ifdef TGFX_TE_STRICT_CONSTRAINT
    , vec4 extraSubset
#endif
#ifdef TGFX_TE_RGBAAA
    , vec2 alphaStart
#endif
) {
    vec2 finalCoord = TGFX_TE_ClampCoord(texCoord
#ifdef TGFX_TE_STRICT_CONSTRAINT
        , extraSubset
#endif
#ifdef TGFX_TE_SUBSET
        , subset
#endif
    );
    vec3 yuv;
    yuv.x = texture(samplerY, finalCoord).r;
    finalCoord = TGFX_TE_ClampCoord(texCoord
#ifdef TGFX_TE_STRICT_CONSTRAINT
        , extraSubset
#endif
#ifdef TGFX_TE_SUBSET
        , subset
#endif
    );
    yuv.yz = texture(samplerUV, finalCoord).ra;
#ifdef TGFX_TE_YUV_LIMITED_RANGE
    yuv.x -= (16.0 / 255.0);
#endif
    yuv.yz -= vec2(0.5, 0.5);
    vec3 rgb = clamp(colorConversion * yuv, 0.0, 1.0);
#ifdef TGFX_TE_RGBAAA
    vec2 alphaVertexColor = finalCoord + alphaStart;
    float yuv_a = texture(samplerY, alphaVertexColor).r;
    yuv_a = (yuv_a - 16.0 / 255.0) / (219.0 / 255.0 - 1.0 / 255.0);
    yuv_a = clamp(yuv_a, 0.0, 1.0);
    return vec4(rgb * yuv_a, yuv_a);
#else
    return vec4(rgb, 1.0);
#endif
}

#endif
)GLSL";

static const std::string kTiledTextureEffect = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// tiled_texture_effect.frag.glsl - TiledTextureEffect modular shader function.
// Handles tiled/repeated/mirrored/border-clamped texture sampling with per-axis modes.
//
// Required macros:
//   TGFX_TTE_MODE_X - ShaderMode for X axis (0-8)
//   TGFX_TTE_MODE_Y - ShaderMode for Y axis (0-8)
//
// ShaderMode values:
//   0 = None
//   1 = Clamp
//   2 = RepeatNearestNone
//   3 = RepeatLinearNone
//   4 = RepeatLinearMipmap
//   5 = RepeatNearestMipmap
//   6 = MirrorRepeat
//   7 = ClampToBorderNearest
//   8 = ClampToBorderLinear
//
// Optional macros:
//   TGFX_TTE_STRICT_CONSTRAINT - defined if extra strict subset clamping is needed
//   TGFX_TTE_ALPHA_ONLY        - defined if texture is alpha-only
//   TGFX_TTE_HAS_DIMENSION     - defined if normalization dimension uniform is needed
//   TGFX_TTE_HAS_SUBSET        - defined if subset uniform is needed
//   TGFX_TTE_HAS_CLAMP         - defined if clamp uniform is needed

#ifndef TGFX_TTE_MODE_X
#define TGFX_TTE_MODE_X 0
#endif
#ifndef TGFX_TTE_MODE_Y
#define TGFX_TTE_MODE_Y 0
#endif

#ifndef TGFX_TTE_SAMPLER_TYPE
#define TGFX_TTE_SAMPLER_TYPE sampler2D
#endif

// Helper: does mode require unorm coord?
// Modes 3,4,5,8 require it
#define TGFX_TTE_UNORM_X (TGFX_TTE_MODE_X == 3 || TGFX_TTE_MODE_X == 4 || TGFX_TTE_MODE_X == 5 || TGFX_TTE_MODE_X == 8)
#define TGFX_TTE_UNORM_Y (TGFX_TTE_MODE_Y == 3 || TGFX_TTE_MODE_Y == 4 || TGFX_TTE_MODE_Y == 5 || TGFX_TTE_MODE_Y == 8)

// Helper: does mode use subset?
// All modes except 0 (None), 1 (Clamp), 8 (ClampToBorderLinear)
#define TGFX_TTE_USES_SUBSET_X (TGFX_TTE_MODE_X != 0 && TGFX_TTE_MODE_X != 1 && TGFX_TTE_MODE_X != 8)
#define TGFX_TTE_USES_SUBSET_Y (TGFX_TTE_MODE_Y != 0 && TGFX_TTE_MODE_Y != 1 && TGFX_TTE_MODE_Y != 8)

// Helper: does mode use clamp?
// All modes except 0 (None), 7 (ClampToBorderNearest)
#define TGFX_TTE_USES_CLAMP_X (TGFX_TTE_MODE_X != 0 && TGFX_TTE_MODE_X != 7)
#define TGFX_TTE_USES_CLAMP_Y (TGFX_TTE_MODE_Y != 0 && TGFX_TTE_MODE_Y != 7)

// Helper: is mipmap repeat mode?
#define TGFX_TTE_MIPMAP_X (TGFX_TTE_MODE_X == 4 || TGFX_TTE_MODE_X == 5)
#define TGFX_TTE_MIPMAP_Y (TGFX_TTE_MODE_Y == 4 || TGFX_TTE_MODE_Y == 5)

// Helper: is linear repeat mode (for error-based sub-texel blending)?
#define TGFX_TTE_LINEAR_REPEAT_X (TGFX_TTE_MODE_X == 3 || TGFX_TTE_MODE_X == 4)
#define TGFX_TTE_LINEAR_REPEAT_Y (TGFX_TTE_MODE_Y == 3 || TGFX_TTE_MODE_Y == 4)

vec4 TGFX_TiledTextureEffect(vec4 inputColor, vec2 texCoord, TGFX_TTE_SAMPLER_TYPE textureSampler
#ifdef TGFX_TTE_HAS_SUBSET
    , vec4 subset
#endif
#ifdef TGFX_TTE_HAS_CLAMP
    , vec4 clampRect
#endif
#ifdef TGFX_TTE_HAS_DIMENSION
    , vec2 dimension
#endif
#ifdef TGFX_TTE_STRICT_CONSTRAINT
    , vec4 extraSubset
#endif
) {
    // Fast path: both modes are None
#if TGFX_TTE_MODE_X == 0 && TGFX_TTE_MODE_Y == 0
    vec4 result = texture(textureSampler, texCoord);
#else
    vec2 inCoord = texCoord;

#ifdef TGFX_TTE_HAS_DIMENSION
    inCoord /= dimension;
#endif

    // Mipmap repeat variables
#if TGFX_TTE_MIPMAP_X || TGFX_TTE_MIPMAP_Y
    vec2 extraRepeatCoord;
#endif
#if TGFX_TTE_MIPMAP_X
    float repeatCoordWeightX;
#endif
#if TGFX_TTE_MIPMAP_Y
    float repeatCoordWeightY;
#endif

    // --- Subset coordinate computation ---
    highp vec2 subsetCoord;

    // X axis subset coord
#if TGFX_TTE_MODE_X == 0 || TGFX_TTE_MODE_X == 7 || TGFX_TTE_MODE_X == 8 || TGFX_TTE_MODE_X == 1
    subsetCoord.x = inCoord.x;
#elif TGFX_TTE_MODE_X == 2 || TGFX_TTE_MODE_X == 3
    subsetCoord.x = mod(inCoord.x - subset.x, subset.z - subset.x) + subset.x;
#elif TGFX_TTE_MODE_X == 4 || TGFX_TTE_MODE_X == 5
    {
        float w = subset.z - subset.x;
        float w2 = 2.0 * w;
        float d = inCoord.x - subset.x;
        float m = mod(d, w2);
        float o = mix(m, w2 - m, step(w, m));
        subsetCoord.x = o + subset.x;
        extraRepeatCoord.x = w - o + subset.x;
        float hw = w / 2.0;
        float n = mod(d - hw, w2);
        repeatCoordWeightX = clamp(mix(n, w2 - n, step(w, n)) - hw + 0.5, 0.0, 1.0);
    }
#elif TGFX_TTE_MODE_X == 6
    {
        float w = subset.z - subset.x;
        float w2 = 2.0 * w;
        float m = mod(inCoord.x - subset.x, w2);
        subsetCoord.x = mix(m, w2 - m, step(w, m)) + subset.x;
    }
#endif

    // Y axis subset coord
#if TGFX_TTE_MODE_Y == 0 || TGFX_TTE_MODE_Y == 7 || TGFX_TTE_MODE_Y == 8 || TGFX_TTE_MODE_Y == 1
    subsetCoord.y = inCoord.y;
#elif TGFX_TTE_MODE_Y == 2 || TGFX_TTE_MODE_Y == 3
    subsetCoord.y = mod(inCoord.y - subset.y, subset.w - subset.y) + subset.y;
#elif TGFX_TTE_MODE_Y == 4 || TGFX_TTE_MODE_Y == 5
    {
        float w = subset.w - subset.y;
        float w2 = 2.0 * w;
        float d = inCoord.y - subset.y;
        float m = mod(d, w2);
        float o = mix(m, w2 - m, step(w, m));
        subsetCoord.y = o + subset.y;
        extraRepeatCoord.y = w - o + subset.y;
        float hw = w / 2.0;
        float n = mod(d - hw, w2);
        repeatCoordWeightY = clamp(mix(n, w2 - n, step(w, n)) - hw + 0.5, 0.0, 1.0);
    }
#elif TGFX_TTE_MODE_Y == 6
    {
        float w = subset.w - subset.y;
        float w2 = 2.0 * w;
        float m = mod(inCoord.y - subset.y, w2);
        subsetCoord.y = mix(m, w2 - m, step(w, m)) + subset.y;
    }
#endif

    // --- Clamp coordinate computation ---
    highp vec2 clampedCoord;

#if TGFX_TTE_USES_CLAMP_X && TGFX_TTE_USES_CLAMP_Y
    // Both axes use clamp
    clampedCoord = clamp(subsetCoord, clampRect.xy, clampRect.zw);
#elif TGFX_TTE_USES_CLAMP_X
    clampedCoord.x = clamp(subsetCoord.x, clampRect.x, clampRect.z);
    clampedCoord.y = subsetCoord.y;
#elif TGFX_TTE_USES_CLAMP_Y
    clampedCoord.x = subsetCoord.x;
    clampedCoord.y = clamp(subsetCoord.y, clampRect.y, clampRect.w);
#else
    clampedCoord = subsetCoord;
#endif

    // --- Strict constraint clamping ---
#ifdef TGFX_TTE_STRICT_CONSTRAINT
    {
#ifdef TGFX_TTE_HAS_DIMENSION
        highp vec4 normExtraSubset = extraSubset;
        normExtraSubset.xy /= dimension;
        normExtraSubset.zw /= dimension;
        clampedCoord = clamp(clampedCoord, normExtraSubset.xy, normExtraSubset.zw);
#else
        clampedCoord = clamp(clampedCoord, extraSubset.xy, extraSubset.zw);
#endif
    }
#endif

    // --- Mipmap repeat extra coord clamping ---
#if TGFX_TTE_MIPMAP_X && TGFX_TTE_MIPMAP_Y
    extraRepeatCoord = clamp(extraRepeatCoord, clampRect.xy, clampRect.zw);
#elif TGFX_TTE_MIPMAP_X
    extraRepeatCoord.x = clamp(extraRepeatCoord.x, clampRect.x, clampRect.z);
#elif TGFX_TTE_MIPMAP_Y
    extraRepeatCoord.y = clamp(extraRepeatCoord.y, clampRect.y, clampRect.w);
#endif

    // --- Texture read with mipmap blending ---
#if TGFX_TTE_MIPMAP_X && TGFX_TTE_MIPMAP_Y
#ifdef TGFX_TTE_HAS_DIMENSION
    vec4 textureColor1 = texture(textureSampler, clampedCoord * dimension);
    vec4 textureColor2 = texture(textureSampler, vec2(extraRepeatCoord.x, clampedCoord.y) * dimension);
    vec4 textureColor3 = texture(textureSampler, vec2(clampedCoord.x, extraRepeatCoord.y) * dimension);
    vec4 textureColor4 = texture(textureSampler, vec2(extraRepeatCoord.x, extraRepeatCoord.y) * dimension);
#else
    vec4 textureColor1 = texture(textureSampler, clampedCoord);
    vec4 textureColor2 = texture(textureSampler, vec2(extraRepeatCoord.x, clampedCoord.y));
    vec4 textureColor3 = texture(textureSampler, vec2(clampedCoord.x, extraRepeatCoord.y));
    vec4 textureColor4 = texture(textureSampler, vec2(extraRepeatCoord.x, extraRepeatCoord.y));
#endif
    vec4 textureColor = mix(mix(textureColor1, textureColor2, repeatCoordWeightX),
                            mix(textureColor3, textureColor4, repeatCoordWeightX), repeatCoordWeightY);
#elif TGFX_TTE_MIPMAP_X
#ifdef TGFX_TTE_HAS_DIMENSION
    vec4 textureColor1 = texture(textureSampler, clampedCoord * dimension);
    vec4 textureColor2 = texture(textureSampler, vec2(extraRepeatCoord.x, clampedCoord.y) * dimension);
#else
    vec4 textureColor1 = texture(textureSampler, clampedCoord);
    vec4 textureColor2 = texture(textureSampler, vec2(extraRepeatCoord.x, clampedCoord.y));
#endif
    vec4 textureColor = mix(textureColor1, textureColor2, repeatCoordWeightX);
#elif TGFX_TTE_MIPMAP_Y
#ifdef TGFX_TTE_HAS_DIMENSION
    vec4 textureColor1 = texture(textureSampler, clampedCoord * dimension);
    vec4 textureColor2 = texture(textureSampler, vec2(clampedCoord.x, extraRepeatCoord.y) * dimension);
#else
    vec4 textureColor1 = texture(textureSampler, clampedCoord);
    vec4 textureColor2 = texture(textureSampler, vec2(clampedCoord.x, extraRepeatCoord.y));
#endif
    vec4 textureColor = mix(textureColor1, textureColor2, repeatCoordWeightY);
#else
    // Standard read - normalize if dimension is available
#ifdef TGFX_TTE_HAS_DIMENSION
    vec4 textureColor = texture(textureSampler, clampedCoord * dimension);
#else
    vec4 textureColor = texture(textureSampler, clampedCoord);
#endif
#endif

    // --- Linear repeat error-based blending ---
#if TGFX_TTE_LINEAR_REPEAT_X || TGFX_TTE_MODE_X == 8
    float errX = subsetCoord.x - clampedCoord.x;
#if TGFX_TTE_LINEAR_REPEAT_X
    float repeatCoordX = errX > 0.0 ? clampRect.x : clampRect.z;
#endif
#endif

#if TGFX_TTE_LINEAR_REPEAT_Y || TGFX_TTE_MODE_Y == 8
    float errY = subsetCoord.y - clampedCoord.y;
#if TGFX_TTE_LINEAR_REPEAT_Y
    float repeatCoordY = errY > 0.0 ? clampRect.y : clampRect.w;
#endif
#endif

    // Handle RepeatLinear blending for both axes
#if TGFX_TTE_LINEAR_REPEAT_X && TGFX_TTE_LINEAR_REPEAT_Y
    {
#ifdef TGFX_TTE_HAS_DIMENSION
        vec4 repeatReadX = texture(textureSampler, vec2(repeatCoordX, clampedCoord.y) * dimension);
        vec4 repeatReadY = texture(textureSampler, vec2(clampedCoord.x, repeatCoordY) * dimension);
        vec4 repeatReadXY = texture(textureSampler, vec2(repeatCoordX, repeatCoordY) * dimension);
#else
        vec4 repeatReadX = texture(textureSampler, vec2(repeatCoordX, clampedCoord.y));
        vec4 repeatReadY = texture(textureSampler, vec2(clampedCoord.x, repeatCoordY));
        vec4 repeatReadXY = texture(textureSampler, vec2(repeatCoordX, repeatCoordY));
#endif
        if (errX != 0.0 && errY != 0.0) {
            errX = abs(errX);
            textureColor = mix(mix(textureColor, repeatReadX, errX),
                               mix(repeatReadY, repeatReadXY, errX), abs(errY));
        } else if (errX != 0.0) {
#ifdef TGFX_TTE_HAS_DIMENSION
            vec4 rX = texture(textureSampler, vec2(repeatCoordX, clampedCoord.y) * dimension);
#else
            vec4 rX = texture(textureSampler, vec2(repeatCoordX, clampedCoord.y));
#endif
            textureColor = mix(textureColor, rX, errX);
        } else if (errY != 0.0) {
#ifdef TGFX_TTE_HAS_DIMENSION
            vec4 rY = texture(textureSampler, vec2(clampedCoord.x, repeatCoordY) * dimension);
#else
            vec4 rY = texture(textureSampler, vec2(clampedCoord.x, repeatCoordY));
#endif
            textureColor = mix(textureColor, rY, errY);
        }
    }
#elif TGFX_TTE_LINEAR_REPEAT_X
    if (errX != 0.0) {
#ifdef TGFX_TTE_HAS_DIMENSION
        vec4 repeatReadX = texture(textureSampler, vec2(repeatCoordX, clampedCoord.y) * dimension);
#else
        vec4 repeatReadX = texture(textureSampler, vec2(repeatCoordX, clampedCoord.y));
#endif
        textureColor = mix(textureColor, repeatReadX, errX);
    }
#elif TGFX_TTE_LINEAR_REPEAT_Y
    if (errY != 0.0) {
#ifdef TGFX_TTE_HAS_DIMENSION
        vec4 repeatReadY = texture(textureSampler, vec2(clampedCoord.x, repeatCoordY) * dimension);
#else
        vec4 repeatReadY = texture(textureSampler, vec2(clampedCoord.x, repeatCoordY));
#endif
        textureColor = mix(textureColor, repeatReadY, errY);
    }
#endif

    // --- ClampToBorderLinear fade ---
#if TGFX_TTE_MODE_X == 8
    textureColor = mix(textureColor, vec4(0.0), min(abs(errX), 1.0));
#endif
#if TGFX_TTE_MODE_Y == 8
    textureColor = mix(textureColor, vec4(0.0), min(abs(errY), 1.0));
#endif

    // --- ClampToBorderNearest discard ---
#if TGFX_TTE_MODE_X == 7
    {
        float snappedX = floor(inCoord.x + 0.001) + 0.5;
        if (snappedX < subset.x || snappedX > subset.z) {
            textureColor = vec4(0.0);
        }
    }
#endif
#if TGFX_TTE_MODE_Y == 7
    {
        float snappedY = floor(inCoord.y + 0.001) + 0.5;
        if (snappedY < subset.y || snappedY > subset.w) {
            textureColor = vec4(0.0);
        }
    }
#endif

    vec4 result = textureColor;
#endif  // !(MODE_X == 0 && MODE_Y == 0)

    // --- Alpha-only / premultiply ---
#ifdef TGFX_TTE_ALPHA_ONLY
    // Alpha-only textures (R8 format) store alpha in the red channel.
    result = result.rrrr;
    return result.a * inputColor;
#else
    return result * inputColor.a;
#endif
}
)GLSL";

static const std::string kUnrolledBinaryGradientColorizer = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// unrolled_binary_gradient.frag.glsl - UnrolledBinaryGradientColorizer modular shader function.
// Binary search tree over scale/bias intervals to compute gradient color.
//
// Required macros:
//   TGFX_UBGC_INTERVAL_COUNT - Number of intervals (1-8)

#ifndef TGFX_UBGC_INTERVAL_COUNT
#define TGFX_UBGC_INTERVAL_COUNT 1
#endif

vec4 TGFX_UnrolledBinaryGradientColorizer(vec4 inputColor,
    vec4 scale0_1, vec4 bias0_1,
    vec4 thresholds1_7
#if TGFX_UBGC_INTERVAL_COUNT > 1
    , vec4 scale2_3, vec4 bias2_3
#endif
#if TGFX_UBGC_INTERVAL_COUNT > 2
    , vec4 scale4_5, vec4 bias4_5
#endif
#if TGFX_UBGC_INTERVAL_COUNT > 3
    , vec4 scale6_7, vec4 bias6_7
#endif
#if TGFX_UBGC_INTERVAL_COUNT > 4
    , vec4 scale8_9, vec4 bias8_9, vec4 thresholds9_13
#endif
#if TGFX_UBGC_INTERVAL_COUNT > 5
    , vec4 scale10_11, vec4 bias10_11
#endif
#if TGFX_UBGC_INTERVAL_COUNT > 6
    , vec4 scale12_13, vec4 bias12_13
#endif
#if TGFX_UBGC_INTERVAL_COUNT > 7
    , vec4 scale14_15, vec4 bias14_15
#endif
) {
    float t = inputColor.x;
    vec4 scale, bias;

#if TGFX_UBGC_INTERVAL_COUNT >= 4
    if (t < thresholds1_7.w) {
#endif

    // Binary tree level 1 (intervals 0-3)
#if TGFX_UBGC_INTERVAL_COUNT >= 2
    if (t < thresholds1_7.y) {
#endif
        if (t < thresholds1_7.x) {
            scale = scale0_1;
            bias = bias0_1;
#if TGFX_UBGC_INTERVAL_COUNT > 1
        } else {
            scale = scale2_3;
            bias = bias2_3;
#endif
        }
#if TGFX_UBGC_INTERVAL_COUNT > 2
    } else {
#endif
#if TGFX_UBGC_INTERVAL_COUNT >= 3
        if (t < thresholds1_7.z) {
            scale = scale4_5;
            bias = bias4_5;
#if TGFX_UBGC_INTERVAL_COUNT > 3
        } else {
            scale = scale6_7;
            bias = bias6_7;
#endif
        }
#endif
#if TGFX_UBGC_INTERVAL_COUNT >= 2
    }
#endif

#if TGFX_UBGC_INTERVAL_COUNT > 4
    } else {
#endif

    // Binary tree level 2 (intervals 4-7)
#if TGFX_UBGC_INTERVAL_COUNT >= 6
    if (t < thresholds9_13.y) {
#endif
#if TGFX_UBGC_INTERVAL_COUNT >= 5
        if (t < thresholds9_13.x) {
            scale = scale8_9;
            bias = bias8_9;
#if TGFX_UBGC_INTERVAL_COUNT > 5
        } else {
            scale = scale10_11;
            bias = bias10_11;
#endif
        }
#endif
#if TGFX_UBGC_INTERVAL_COUNT > 6
    } else {
#endif
#if TGFX_UBGC_INTERVAL_COUNT >= 7
        if (t < thresholds9_13.z) {
            scale = scale12_13;
            bias = bias12_13;
#if TGFX_UBGC_INTERVAL_COUNT > 7
        } else {
            scale = scale14_15;
            bias = bias14_15;
#endif
        }
#endif
#if TGFX_UBGC_INTERVAL_COUNT >= 6
    }
#endif

#if TGFX_UBGC_INTERVAL_COUNT >= 4
    }
#endif

    return vec4(t * scale + bias);
}
)GLSL";

static const std::string kBlendModes = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// tgfx_blend.glsl - GLSL implementations of all 15 advanced blend modes.
// Self-contained: no external includes needed.
// All math uses premultiplied alpha. Alpha channel uses src-over for all modes.
//
// Blend mode indices (matching BlendMode enum):
//   15=Overlay, 16=Darken, 17=Lighten, 18=ColorDodge, 19=ColorBurn,
//   20=HardLight, 21=SoftLight, 22=Difference, 23=Exclusion, 24=Multiply,
//   25=Hue, 26=Saturation, 27=Color, 28=Luminosity, 29=PlusDarker

///////////////////////////////////////////////////////////////////////////////
// HSL helper functions (used by Hue, Saturation, Color, Luminosity modes)
///////////////////////////////////////////////////////////////////////////////

float tgfx_luminance(vec3 color) {
  return dot(vec3(0.3, 0.59, 0.11), color);
}

vec3 tgfx_set_luminance(vec3 hueSat, float alpha, vec3 lumColor) {
  float diff = tgfx_luminance(lumColor - hueSat);
  vec3 outColor = hueSat + diff;
  float outLum = tgfx_luminance(outColor);
  float minComp = min(min(outColor.r, outColor.g), outColor.b);
  float maxComp = max(max(outColor.r, outColor.g), outColor.b);
  if (minComp < 0.0 && outLum != minComp) {
    outColor = outLum + ((outColor - vec3(outLum, outLum, outLum)) * outLum) / (outLum - minComp);
  }
  if (maxComp > alpha && maxComp != outLum) {
    outColor = outLum + ((outColor - vec3(outLum, outLum, outLum)) * (alpha - outLum)) / (maxComp - outLum);
  }
  return outColor;
}

float tgfx_saturation(vec3 color) {
  return max(max(color.r, color.g), color.b) - min(min(color.r, color.g), color.b);
}

vec3 tgfx_set_saturation_helper(float minComp, float midComp, float maxComp, float sat) {
  if (minComp < maxComp) {
    vec3 result;
    result.r = 0.0;
    result.g = sat * (midComp - minComp) / (maxComp - minComp);
    result.b = sat;
    return result;
  } else {
    return vec3(0, 0, 0);
  }
}

vec3 tgfx_set_saturation(vec3 hueLumColor, vec3 satColor) {
  float sat = tgfx_saturation(satColor);
  if (hueLumColor.r <= hueLumColor.g) {
    if (hueLumColor.g <= hueLumColor.b) {
      hueLumColor.rgb = tgfx_set_saturation_helper(hueLumColor.r, hueLumColor.g, hueLumColor.b, sat);
    } else if (hueLumColor.r <= hueLumColor.b) {
      hueLumColor.rbg = tgfx_set_saturation_helper(hueLumColor.r, hueLumColor.b, hueLumColor.g, sat);
    } else {
      hueLumColor.brg = tgfx_set_saturation_helper(hueLumColor.b, hueLumColor.r, hueLumColor.g, sat);
    }
  } else if (hueLumColor.r <= hueLumColor.b) {
    hueLumColor.grb = tgfx_set_saturation_helper(hueLumColor.g, hueLumColor.r, hueLumColor.b, sat);
  } else if (hueLumColor.g <= hueLumColor.b) {
    hueLumColor.gbr = tgfx_set_saturation_helper(hueLumColor.g, hueLumColor.b, hueLumColor.r, sat);
  } else {
    hueLumColor.bgr = tgfx_set_saturation_helper(hueLumColor.b, hueLumColor.g, hueLumColor.r, sat);
  }
  return hueLumColor;
}

///////////////////////////////////////////////////////////////////////////////
// Per-component helper functions
///////////////////////////////////////////////////////////////////////////////

// HardLight per-component blend (used by HardLight and Overlay).
// Computes hard light for RGB, then adds the uncovered portions.
vec3 tgfx_hard_light_rgb(vec4 src, vec4 dst) {
  vec3 result;
  // Per-component hard light core
  for (int i = 0; i < 3; i++) {
    float s = (i == 0) ? src.r : ((i == 1) ? src.g : src.b);
    float d = (i == 0) ? dst.r : ((i == 1) ? dst.g : dst.b);
    float f;
    if (2.0 * s < src.a) {
      f = 2.0 * s * d;
    } else {
      f = src.a * dst.a - 2.0 * (dst.a - d) * (src.a - s);
    }
    if (i == 0) result.r = f;
    else if (i == 1) result.g = f;
    else result.b = f;
  }
  // Add uncovered portions
  result.rgb += src.rgb * (1.0 - dst.a) + dst.rgb * (1.0 - src.a);
  return result;
}

// ColorDodge per-component
float tgfx_color_dodge_component(float sc, float sa, float dc, float da) {
  if (0.0 == dc) {
    return sc * (1.0 - da);
  } else {
    float d = sa - sc;
    if (0.0 == d) {
      return sa * da + sc * (1.0 - da) + dc * (1.0 - sa);
    } else {
      d = min(da, dc * sa / d);
      return d * sa + sc * (1.0 - da) + dc * (1.0 - sa);
    }
  }
}

// ColorBurn per-component
float tgfx_color_burn_component(float sc, float sa, float dc, float da) {
  if (da == dc) {
    return sa * da + sc * (1.0 - da) + dc * (1.0 - sa);
  } else if (0.0 == sc) {
    return dc * (1.0 - sa);
  } else {
    float d = max(0.0, da - (da - dc) * sa / sc);
    return sa * d + sc * (1.0 - da) + dc * (1.0 - sa);
  }
}

// SoftLight per-component (assumes dst.a > 0)
float tgfx_soft_light_component(float sc, float sa, float dc, float da) {
  // if (2S <= Sa)
  if (2.0 * sc <= sa) {
    // (D^2 (Sa-2S))/Da + (1-Da)S + D(-Sa+2S+1)
    return (dc * dc * (sa - 2.0 * sc)) / da +
           (1.0 - da) * sc + dc * (-sa + 2.0 * sc + 1.0);
  // else if (4D <= Da)
  } else if (4.0 * dc <= da) {
    float DSqd = dc * dc;
    float DCub = DSqd * dc;
    float DaSqd = da * da;
    float DaCub = DaSqd * da;
    // (Da^2(S - D(3Sa-6S-1)) + 12DaD^2(Sa-2S) - 16D^3(Sa-2S) - Da^3 S) / Da^2
    return (DaSqd * (sc - dc * (3.0 * sa - 6.0 * sc - 1.0)) +
            12.0 * da * DSqd * (sa - 2.0 * sc) - 16.0 * DCub * (sa - 2.0 * sc) -
            DaCub * sc) / DaSqd;
  } else {
    // -sqrt(Da*D)(Sa-2S) - DaS + D(Sa-2S+1) + S
    return dc * (sa - 2.0 * sc + 1.0) + sc -
           sqrt(da * dc) * (sa - 2.0 * sc) - da * sc;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Individual blend mode functions
///////////////////////////////////////////////////////////////////////////////

vec4 tgfx_blend_overlay(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  // Overlay is HardLight with src and dst swapped
  result.rgb = tgfx_hard_light_rgb(dst, src);
  return result;
}

vec4 tgfx_blend_darken(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.rgb = min((1.0 - src.a) * dst.rgb + src.rgb, (1.0 - dst.a) * src.rgb + dst.rgb);
  return result;
}

vec4 tgfx_blend_lighten(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.rgb = max((1.0 - src.a) * dst.rgb + src.rgb, (1.0 - dst.a) * src.rgb + dst.rgb);
  return result;
}

vec4 tgfx_blend_color_dodge(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.r = tgfx_color_dodge_component(src.r, src.a, dst.r, dst.a);
  result.g = tgfx_color_dodge_component(src.g, src.a, dst.g, dst.a);
  result.b = tgfx_color_dodge_component(src.b, src.a, dst.b, dst.a);
  return result;
}

vec4 tgfx_blend_color_burn(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.r = tgfx_color_burn_component(src.r, src.a, dst.r, dst.a);
  result.g = tgfx_color_burn_component(src.g, src.a, dst.g, dst.a);
  result.b = tgfx_color_burn_component(src.b, src.a, dst.b, dst.a);
  return result;
}

vec4 tgfx_blend_hard_light(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.rgb = tgfx_hard_light_rgb(src, dst);
  return result;
}

vec4 tgfx_blend_soft_light(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  if (0.0 == dst.a) {
    result.rgba = src;
  } else {
    result.r = tgfx_soft_light_component(src.r, src.a, dst.r, dst.a);
    result.g = tgfx_soft_light_component(src.g, src.a, dst.g, dst.a);
    result.b = tgfx_soft_light_component(src.b, src.a, dst.b, dst.a);
  }
  return result;
}

vec4 tgfx_blend_difference(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.rgb = src.rgb + dst.rgb - 2.0 * min(src.rgb * dst.a, dst.rgb * src.a);
  return result;
}

vec4 tgfx_blend_exclusion(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.rgb = dst.rgb + src.rgb - 2.0 * dst.rgb * src.rgb;
  return result;
}

vec4 tgfx_blend_multiply(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.rgb = (1.0 - src.a) * dst.rgb + (1.0 - dst.a) * src.rgb + src.rgb * dst.rgb;
  return result;
}

vec4 tgfx_blend_hue(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  vec4 dstSrcAlpha = dst * src.a;
  result.rgb = tgfx_set_luminance(tgfx_set_saturation(src.rgb * dst.a, dstSrcAlpha.rgb), dstSrcAlpha.a, dstSrcAlpha.rgb);
  result.rgb += (1.0 - src.a) * dst.rgb + (1.0 - dst.a) * src.rgb;
  return result;
}

vec4 tgfx_blend_saturation(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  vec4 dstSrcAlpha = dst * src.a;
  result.rgb = tgfx_set_luminance(tgfx_set_saturation(dstSrcAlpha.rgb, src.rgb * dst.a), dstSrcAlpha.a, dstSrcAlpha.rgb);
  result.rgb += (1.0 - src.a) * dst.rgb + (1.0 - dst.a) * src.rgb;
  return result;
}

vec4 tgfx_blend_color(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  vec4 srcDstAlpha = src * dst.a;
  result.rgb = tgfx_set_luminance(srcDstAlpha.rgb, srcDstAlpha.a, dst.rgb * src.a);
  result.rgb += (1.0 - src.a) * dst.rgb + (1.0 - dst.a) * src.rgb;
  return result;
}

vec4 tgfx_blend_luminosity(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  vec4 srcDstAlpha = src * dst.a;
  result.rgb = tgfx_set_luminance(dst.rgb * src.a, srcDstAlpha.a, srcDstAlpha.rgb);
  result.rgb += (1.0 - src.a) * dst.rgb + (1.0 - dst.a) * src.rgb;
  return result;
}

vec4 tgfx_blend_plus_darker(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.rgb = clamp(1.0 + src.rgb + dst.rgb - dst.a - src.a, 0.0, 1.0);
  result.rgb *= (result.a > 0.0) ? 1.0 : 0.0;
  return result;
}

///////////////////////////////////////////////////////////////////////////////
// Main dispatch function
///////////////////////////////////////////////////////////////////////////////

vec4 tgfx_blend(vec4 src, vec4 dst, int mode) {
  if (mode == 15) return tgfx_blend_overlay(src, dst);
  if (mode == 16) return tgfx_blend_darken(src, dst);
  if (mode == 17) return tgfx_blend_lighten(src, dst);
  if (mode == 18) return tgfx_blend_color_dodge(src, dst);
  if (mode == 19) return tgfx_blend_color_burn(src, dst);
  if (mode == 20) return tgfx_blend_hard_light(src, dst);
  if (mode == 21) return tgfx_blend_soft_light(src, dst);
  if (mode == 22) return tgfx_blend_difference(src, dst);
  if (mode == 23) return tgfx_blend_exclusion(src, dst);
  if (mode == 24) return tgfx_blend_multiply(src, dst);
  if (mode == 25) return tgfx_blend_hue(src, dst);
  if (mode == 26) return tgfx_blend_saturation(src, dst);
  if (mode == 27) return tgfx_blend_color(src, dst);
  if (mode == 28) return tgfx_blend_luminosity(src, dst);
  if (mode == 29) return tgfx_blend_plus_darker(src, dst);
  return src; // fallback
}
)GLSL";

static const std::string kEmptyXfer = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// empty_xfer.frag.glsl - EmptyXferProcessor fragment shader.
// Applies coverage to the input color with no blending against destination.
//
// Parameters: inputColor (vec4), inputCoverage (vec4)
// Output: outputColor (vec4)
//
// Required macros:
//   (none)

void TGFX_EmptyXP_FS(vec4 inputColor, vec4 inputCoverage, out vec4 outputColor) {
    outputColor = inputColor * inputCoverage;
}
)GLSL";

static const std::string kPorterDuffXfer = R"GLSL(
// Copyright (C) 2026 Tencent. All rights reserved.
// porter_duff_xfer.frag.glsl - PorterDuffXferProcessor fragment shader.
// Performs Porter-Duff blending of source against destination with coverage modulation.
//
// Parameters: inputColor (vec4), inputCoverage (vec4), dstColor (vec4)
// Output: outputColor (vec4)
//
// Required macros:
//   TGFX_BLEND_MODE - integer selecting the blend formula (0=Clear, 1=Src, 2=Dst, etc.)
//
// Optional macros:
//   TGFX_PDXP_DST_TEXTURE_READ - defined when destination is read from a texture via
//       gl_FragCoord; provides DstTextureUpperLeft (vec2) and DstTextureCoordScale (vec2) uniforms
//       and the DstTextureSampler sampler.
//   TGFX_PDXP_NON_COEFF - defined when the blend mode cannot be expressed as simple
//       src/dst coefficient pairs and requires per-component blend evaluation. When set,
//       coverage is applied as: result = coverage * blended + (1 - coverage) * dst.

#ifdef TGFX_PDXP_DST_TEXTURE_READ
void TGFX_PorterDuffXP_FS(vec4 inputColor, vec4 inputCoverage,
                            vec2 dstTextureUpperLeft, vec2 dstTextureCoordScale,
                            sampler2D dstTextureSampler,
                            out vec4 outputColor) {
    // Discard fully transparent fragments to avoid destination read overhead.
    if (inputCoverage.r <= 0.0 && inputCoverage.g <= 0.0 && inputCoverage.b <= 0.0) {
        discard;
    }

    // Read destination color from the copy texture.
    vec2 dstTexCoord = (gl_FragCoord.xy - dstTextureUpperLeft) * dstTextureCoordScale;
    vec4 dstColor = texture(dstTextureSampler, dstTexCoord);
#else
void TGFX_PorterDuffXP_FS(vec4 inputColor, vec4 inputCoverage, vec4 dstColor,
                            out vec4 outputColor) {
#endif

    // Compute blended result based on blend mode.
    vec4 blended;

    // Common coefficient-based blend modes.
    // Porter-Duff formula: result = srcCoeff * src + dstCoeff * dst
    //   Clear:    srcCoeff = 0, dstCoeff = 0
    //   Src:      srcCoeff = 1, dstCoeff = 0
    //   Dst:      srcCoeff = 0, dstCoeff = 1
    //   SrcOver:  srcCoeff = 1, dstCoeff = (1 - srcAlpha)
    //   DstOver:  srcCoeff = (1 - dstAlpha), dstCoeff = 1
    //   SrcIn:    srcCoeff = dstAlpha, dstCoeff = 0
    //   DstIn:    srcCoeff = 0, dstCoeff = srcAlpha
    //   SrcOut:   srcCoeff = (1 - dstAlpha), dstCoeff = 0
    //   DstOut:   srcCoeff = 0, dstCoeff = (1 - srcAlpha)
    //   SrcATop:  srcCoeff = dstAlpha, dstCoeff = (1 - srcAlpha)
    //   DstATop:  srcCoeff = (1 - dstAlpha), dstCoeff = srcAlpha
    //   Xor:      srcCoeff = (1 - dstAlpha), dstCoeff = (1 - srcAlpha)
    //   Modulate: srcCoeff = 0, dstCoeff = src (component-wise multiply)
    //   Screen:   result = src + dst - src * dst

#if TGFX_BLEND_MODE == 0
    // Clear
    blended = vec4(0.0);
#elif TGFX_BLEND_MODE == 1
    // Src
    blended = inputColor;
#elif TGFX_BLEND_MODE == 2
    // Dst
    blended = dstColor;
#elif TGFX_BLEND_MODE == 3
    // SrcOver
    blended = inputColor + (1.0 - inputColor.a) * dstColor;
#elif TGFX_BLEND_MODE == 4
    // DstOver
    blended = (1.0 - dstColor.a) * inputColor + dstColor;
#elif TGFX_BLEND_MODE == 5
    // SrcIn
    blended = dstColor.a * inputColor;
#elif TGFX_BLEND_MODE == 6
    // DstIn
    blended = inputColor.a * dstColor;
#elif TGFX_BLEND_MODE == 7
    // SrcOut
    blended = (1.0 - dstColor.a) * inputColor;
#elif TGFX_BLEND_MODE == 8
    // DstOut
    blended = (1.0 - inputColor.a) * dstColor;
#elif TGFX_BLEND_MODE == 9
    // SrcATop
    blended = dstColor.a * inputColor + (1.0 - inputColor.a) * dstColor;
#elif TGFX_BLEND_MODE == 10
    // DstATop
    blended = (1.0 - dstColor.a) * inputColor + inputColor.a * dstColor;
#elif TGFX_BLEND_MODE == 11
    // Xor
    blended = (1.0 - dstColor.a) * inputColor + (1.0 - inputColor.a) * dstColor;
#elif TGFX_BLEND_MODE == 12
    // PlusLighter
    blended = min(inputColor + dstColor, vec4(1.0));
#elif TGFX_BLEND_MODE == 13
    // Modulate
    blended = inputColor * dstColor;
#elif TGFX_BLEND_MODE == 14
    // Screen
    blended = inputColor + dstColor - inputColor * dstColor;
#else
    // Advanced blend modes (15=Overlay through 29=PlusDarker).
    // These require the tgfx_blend utility functions (included via BlendModes module).
#if TGFX_BLEND_MODE == 15
    blended = tgfx_blend_overlay(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 16
    blended = tgfx_blend_darken(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 17
    blended = tgfx_blend_lighten(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 18
    blended = tgfx_blend_color_dodge(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 19
    blended = tgfx_blend_color_burn(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 20
    blended = tgfx_blend_hard_light(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 21
    blended = tgfx_blend_soft_light(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 22
    blended = tgfx_blend_difference(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 23
    blended = tgfx_blend_exclusion(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 24
    blended = tgfx_blend_multiply(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 25
    blended = tgfx_blend_hue(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 26
    blended = tgfx_blend_saturation(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 27
    blended = tgfx_blend_color(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 28
    blended = tgfx_blend_luminosity(inputColor, dstColor);
#elif TGFX_BLEND_MODE == 29
    blended = tgfx_blend_plus_darker(inputColor, dstColor);
#else
    blended = inputColor + (1.0 - inputColor.a) * dstColor;  // Fallback to SrcOver
#endif
#endif

#ifdef TGFX_PDXP_NON_COEFF
    // For non-coefficient blend modes, apply coverage as a lerp between blended and dst.
    outputColor = inputCoverage * blended + (vec4(1.0) - inputCoverage) * dstColor;
#else
    outputColor = blended;
#endif
}
)GLSL";

static const std::string kXfermodeEffect = R"GLSL(
vec4 tgfx_xfp_blend(vec4 src, vec4 dst) {
#if TGFX_BLEND_MODE == 0
    return vec4(0.0);
#elif TGFX_BLEND_MODE == 1
    return src;
#elif TGFX_BLEND_MODE == 2
    return dst;
#elif TGFX_BLEND_MODE == 3
    return src + (1.0 - src.a) * dst;
#elif TGFX_BLEND_MODE == 4
    return (1.0 - dst.a) * src + dst;
#elif TGFX_BLEND_MODE == 5
    return dst.a * src;
#elif TGFX_BLEND_MODE == 6
    return src.a * dst;
#elif TGFX_BLEND_MODE == 7
    return (1.0 - dst.a) * src;
#elif TGFX_BLEND_MODE == 8
    return (1.0 - src.a) * dst;
#elif TGFX_BLEND_MODE == 9
    return dst.a * src + (1.0 - src.a) * dst;
#elif TGFX_BLEND_MODE == 10
    return (1.0 - dst.a) * src + src.a * dst;
#elif TGFX_BLEND_MODE == 11
    return (1.0 - dst.a) * src + (1.0 - src.a) * dst;
#elif TGFX_BLEND_MODE == 12
    return min(src + dst, vec4(1.0));
#elif TGFX_BLEND_MODE == 13
    return src * dst;
#elif TGFX_BLEND_MODE == 14
    return src + dst - src * dst;
#elif TGFX_BLEND_MODE == 15
    return tgfx_blend_overlay(src, dst);
#elif TGFX_BLEND_MODE == 16
    return tgfx_blend_darken(src, dst);
#elif TGFX_BLEND_MODE == 17
    return tgfx_blend_lighten(src, dst);
#elif TGFX_BLEND_MODE == 18
    return tgfx_blend_color_dodge(src, dst);
#elif TGFX_BLEND_MODE == 19
    return tgfx_blend_color_burn(src, dst);
#elif TGFX_BLEND_MODE == 20
    return tgfx_blend_hard_light(src, dst);
#elif TGFX_BLEND_MODE == 21
    return tgfx_blend_soft_light(src, dst);
#elif TGFX_BLEND_MODE == 22
    return tgfx_blend_difference(src, dst);
#elif TGFX_BLEND_MODE == 23
    return tgfx_blend_exclusion(src, dst);
#elif TGFX_BLEND_MODE == 24
    return tgfx_blend_multiply(src, dst);
#elif TGFX_BLEND_MODE == 25
    return tgfx_blend_hue(src, dst);
#elif TGFX_BLEND_MODE == 26
    return tgfx_blend_saturation(src, dst);
#elif TGFX_BLEND_MODE == 27
    return tgfx_blend_color(src, dst);
#elif TGFX_BLEND_MODE == 28
    return tgfx_blend_luminosity(src, dst);
#elif TGFX_BLEND_MODE == 29
    return tgfx_blend_plus_darker(src, dst);
#else
    return src + (1.0 - src.a) * dst;
#endif
}

#if TGFX_XFP_CHILD_MODE == 2
vec4 TGFX_XfermodeFragmentProcessor(vec4 inputColor, vec4 childSrc, vec4 childDst) {
    vec4 result = tgfx_xfp_blend(childSrc, childDst);
    result *= inputColor.a;
    return result;
}
#elif TGFX_XFP_CHILD_MODE == 0
vec4 TGFX_XfermodeFragmentProcessor(vec4 inputColor, vec4 childDst) {
    return tgfx_xfp_blend(inputColor, childDst);
}
#elif TGFX_XFP_CHILD_MODE == 1
vec4 TGFX_XfermodeFragmentProcessor(vec4 inputColor, vec4 childSrc) {
    return tgfx_xfp_blend(childSrc, inputColor);
}
#endif
)GLSL";

static const std::string kClampedGradientEffect = R"GLSL(
vec4 TGFX_ClampedGradientEffect(vec4 inputColor, vec4 gradLayoutResult,
                                  vec4 colorizerResult,
                                  vec4 leftBorderColor, vec4 rightBorderColor) {
    vec4 t = gradLayoutResult;
    vec4 output;
    if (t.y < 0.0) {
        output = vec4(0.0);
    } else if (t.x <= 0.0) {
        output = leftBorderColor;
    } else if (t.x >= 1.0) {
        output = rightBorderColor;
    } else {
        output = colorizerResult;
    }
    output.rgb *= output.a;
    output *= inputColor.a;
    return output;
}
)GLSL";

static const std::string kColorSpaceXformEffect = R"GLSL(
#ifdef TGFX_CSX_SRC_TF
float tgfx_csx_src_tf(float x, vec4 tf0, vec4 tf1) {
    float G = tf0[0], A = tf0[1], B = tf0[2], C = tf0[3];
    float D = tf1[0], E = tf1[1], F = tf1[2];
    float s = sign(x);
    x = abs(x);
#if TGFX_CSX_SRC_TF_TYPE == 1
    x = (x < D) ? (C * x) + F : pow(A * x + B, G) + E;
#elif TGFX_CSX_SRC_TF_TYPE == 2
    x = pow(max(A + B * pow(x, C), 0.0) / (D + E * pow(x, C)), F);
#elif TGFX_CSX_SRC_TF_TYPE == 3
    x = (x * A <= 1.0) ? pow(x * A, B) : exp((x - E) * C) + D;
    x *= (F + 1.0);
#elif TGFX_CSX_SRC_TF_TYPE == 4
    x /= (F + 1.0);
    x = (x <= 1.0) ? A * pow(x, B) : C * log(x - D) + E;
#endif
    return s * x;
}
#endif

#ifdef TGFX_CSX_DST_TF
float tgfx_csx_dst_tf(float x, vec4 tf0, vec4 tf1) {
    float G = tf0[0], A = tf0[1], B = tf0[2], C = tf0[3];
    float D = tf1[0], E = tf1[1], F = tf1[2];
    float s = sign(x);
    x = abs(x);
#if TGFX_CSX_DST_TF_TYPE == 1
    x = (x < D) ? (C * x) + F : pow(A * x + B, G) + E;
#elif TGFX_CSX_DST_TF_TYPE == 2
    x = pow(max(A + B * pow(x, C), 0.0) / (D + E * pow(x, C)), F);
#elif TGFX_CSX_DST_TF_TYPE == 3
    x = (x * A <= 1.0) ? pow(x * A, B) : exp((x - E) * C) + D;
    x *= (F + 1.0);
#elif TGFX_CSX_DST_TF_TYPE == 4
    x /= (F + 1.0);
    x = (x <= 1.0) ? A * pow(x, B) : C * log(x - D) + E;
#endif
    return s * x;
}
#endif

#ifdef TGFX_CSX_SRC_OOTF
vec3 tgfx_csx_src_ootf(vec3 color, vec4 params) {
    float Y = dot(color, params.rgb);
    return color * sign(Y) * pow(abs(Y), params.a);
}
#endif

#ifdef TGFX_CSX_DST_OOTF
vec3 tgfx_csx_dst_ootf(vec3 color, vec4 params) {
    float Y = dot(color, params.rgb);
    return color * sign(Y) * pow(abs(Y), params.a);
}
#endif

vec4 TGFX_ColorSpaceXformEffect(vec4 color,
                                 vec4 srcTF0, vec4 srcTF1,
                                 vec4 srcOOTFParams,
                                 mat3 colorXform,
                                 vec4 dstOOTFParams,
                                 vec4 dstTF0, vec4 dstTF1) {
#ifdef TGFX_CSX_UNPREMUL
    float alpha = color.a;
    color = alpha > 0.0 ? vec4(color.rgb / alpha, alpha) : vec4(0.0);
#endif
#ifdef TGFX_CSX_SRC_TF
    color.r = tgfx_csx_src_tf(color.r, srcTF0, srcTF1);
    color.g = tgfx_csx_src_tf(color.g, srcTF0, srcTF1);
    color.b = tgfx_csx_src_tf(color.b, srcTF0, srcTF1);
#endif
#ifdef TGFX_CSX_SRC_OOTF
    color.rgb = tgfx_csx_src_ootf(color.rgb, srcOOTFParams);
#endif
#ifdef TGFX_CSX_GAMUT_XFORM
    color.rgb = colorXform * color.rgb;
#endif
#ifdef TGFX_CSX_DST_OOTF
    color.rgb = tgfx_csx_dst_ootf(color.rgb, dstOOTFParams);
#endif
#ifdef TGFX_CSX_DST_TF
    color.r = tgfx_csx_dst_tf(color.r, dstTF0, dstTF1);
    color.g = tgfx_csx_dst_tf(color.g, dstTF0, dstTF1);
    color.b = tgfx_csx_dst_tf(color.b, dstTF0, dstTF1);
#endif
#ifdef TGFX_CSX_PREMUL
    color.rgb *= color.a;
#endif
    return color;
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
    {"AlphaStepFragmentProcessor", ShaderModuleID::AlphaThreshold},
    {"TextureGradientColorizer", ShaderModuleID::TextureGradientColorizer},
    {"DeviceSpaceTextureEffect", ShaderModuleID::DeviceSpaceTextureEffect},
    {"TextureEffect", ShaderModuleID::TextureEffect},
    {"TiledTextureEffect", ShaderModuleID::TiledTextureEffect},
    {"UnrolledBinaryGradientColorizer", ShaderModuleID::UnrolledBinaryGradientColorizer},
    {"EmptyXferProcessor", ShaderModuleID::EmptyXfer},
    {"PorterDuffXferProcessor", ShaderModuleID::PorterDuffXfer},
    {"XfermodeFragmentProcessor - two", ShaderModuleID::XfermodeEffect},
    {"XfermodeFragmentProcessor - dst", ShaderModuleID::XfermodeEffect},
    {"XfermodeFragmentProcessor - src", ShaderModuleID::XfermodeEffect},
    {"ClampedGradientEffect", ShaderModuleID::ClampedGradientEffect},
    {"ColorSpaceXformEffect", ShaderModuleID::ColorSpaceXformEffect},
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
    case ShaderModuleID::TextureEffect:
      return kTextureEffect;
    case ShaderModuleID::TiledTextureEffect:
      return kTiledTextureEffect;
    case ShaderModuleID::UnrolledBinaryGradientColorizer:
      return kUnrolledBinaryGradientColorizer;
    case ShaderModuleID::BlendModes:
      return kBlendModes;
    case ShaderModuleID::EmptyXfer:
      return kEmptyXfer;
    case ShaderModuleID::PorterDuffXfer:
      return kPorterDuffXfer;
    case ShaderModuleID::XfermodeEffect:
      return kXfermodeEffect;
    case ShaderModuleID::ClampedGradientEffect:
      return kClampedGradientEffect;
    case ShaderModuleID::ColorSpaceXformEffect:
      return kColorSpaceXformEffect;
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
