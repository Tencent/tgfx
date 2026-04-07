// Copyright (C) 2026 Tencent. All rights reserved.
// tgfx_types_glsl.glsl - GLSL backend type definitions.
// These macros map abstract tgfx types to GLSL native types.
// For GLSL ES / Desktop GLSL, these are identity mappings.

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
