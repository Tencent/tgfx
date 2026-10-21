// AtlasTextFillShader fragment entry: AtlasTextGeometryProcessor + EmptyXferProcessor/
// PorterDuffXP. The atlas texture carries the coverage (the glyph mask) and, for a color atlas
// (color emoji), the color itself — the shared pipeline's FILL_COVERAGE_ATLAS_TEXT step.
// ALPHA_ONLY is a runtime uniform (AlphaOnly) written by GLSLAtlasTextGeometryProcessor::setData,
// not a compile-time permutation: it is pure fragment math (the vertex stage never uses it), so
// folding it into a uniform branch shrinks the variant count. The analytic draw clip and the
// device-space mask ride the shared runtime clip contract uniforms.
// Fragment dimensions: HAS_COVERAGE, HAS_COMMON_COLOR, HAS_XP (int, 3).
#version 450

#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#define HAS_RUNTIME_DEVICE_MASK 1
#define HAS_RUNTIME_CLIP 1

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#if HAS_COMMON_COLOR
  vec4 Color;
#endif
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
  int AlphaOnly;
};

layout(location = 0) in vec2 vTextureCoords;
#if HAS_COVERAGE
layout(location = 1) in float vCoverage;
#if !HAS_COMMON_COLOR
layout(location = 2) in vec4 vColor;
#endif
#else
#if !HAS_COMMON_COLOR
layout(location = 1) in vec4 vColor;
#endif
#endif

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;
// Always bound: an absent device mask is padded with the shared dummy texture and HasDeviceMask
// is 0.
layout(set = 1, binding = 1) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING 2

#if !HAS_COMMON_COLOR
#define FILL_COLOR FILL_COLOR_VARYING
#endif
#define FILL_COVERAGE FILL_COVERAGE_ATLAS_TEXT
#define FILL_DEVICE_MASK FILL_DEVICE_MASK_RUNTIME
#define FILL_CLIP 1
#include "fill_fragment_main.inc"
