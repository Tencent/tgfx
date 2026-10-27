// AtlasTextFillShader fragment entry: AtlasTextGeometryProcessor + EmptyXferProcessor/
// PorterDuffXP. The atlas texture carries the coverage (the glyph mask) and, for a color atlas
// (color emoji), the color itself — the shared pipeline's FILL_COVERAGE_ATLAS_TEXT step.
// ALPHA_ONLY is a runtime uniform (AlphaOnly) written by GLSLAtlasTextGeometryProcessor::setData,
// not a compile-time permutation: it is pure fragment math (the vertex stage never uses it), so
// folding it into a uniform branch shrinks the variant count. The analytic draw clip and the
// device-space mask ride the shared runtime clip contract uniforms. The color attribute is
// unconditional: the exact Color uniform serves common-color draws through the HasCommonColor
// runtime uniform, the vColor varying per-vertex-colored ones.
// Fragment dimensions: HAS_COVERAGE, HAS_XP (int, 3).
#version 450

#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#define HAS_RUNTIME_DEVICE_MASK 1
#define HAS_RUNTIME_CLIP 1

layout(std140, set = TGFX_FRAGMENT_UBO_SET, binding = TGFX_FRAGMENT_UBO_BINDING) uniform FragmentUniformBlock {
  // Exact paint color for common-color draws; the broadcast into the per-vertex attribute is
  // 8-bit quantized, so HasCommonColor selects this uniform when set.
  vec4 Color;
  int HasCommonColor;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
  int AlphaOnly;
};

layout(location = 0) in vec2 vTextureCoords;
#if HAS_COVERAGE
layout(location = 1) in float vCoverage;
layout(location = 2) in vec4 vColor;
#else
layout(location = 1) in vec4 vColor;
#endif

layout(set = TGFX_TEXTURE_SET, binding = 0) uniform sampler2D TextureSampler_0;
// Always bound: an absent device mask is padded with the shared dummy texture and HasDeviceMask
// is 0.
layout(set = TGFX_TEXTURE_SET, binding = 1) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING 2

#define FILL_COLOR FILL_COLOR_VARYING_OR_COMMON
#define FILL_COVERAGE FILL_COVERAGE_ATLAS_TEXT
#define FILL_DEVICE_MASK FILL_DEVICE_MASK_RUNTIME
#define FILL_CLIP 1
#include "fill_fragment_main.inc"
