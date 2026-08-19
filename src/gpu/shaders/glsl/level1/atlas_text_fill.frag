// AtlasTextFillShader fragment shader
// Processor layout: AtlasTextGeometryProcessor() + EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (injected as #define 0/1):
//   HAS_COVERAGE, HAS_COMMON_COLOR, HAS_XP
// ALPHA_ONLY is a runtime uniform (AlphaOnly), written by GLSLAtlasTextGeometryProcessor::setData,
// not a compile-time permutation: it is pure fragment math (the vertex stage never uses it), so
// folding it into a uniform branch shrinks the variant count.
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
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  // Determine output color from GP
#if HAS_COMMON_COLOR
  vec4 outputColor = Color;
#else
  vec4 outputColor = vColor;
#endif

  // Determine output coverage from GP
#if HAS_COVERAGE
  vec4 outputCoverage = vec4(vCoverage);
#else
  vec4 outputCoverage = vec4(1.0);
#endif

  // Sample atlas texture
  vec4 texColor = texture(TextureSampler_0, vTextureCoords);

  if (AlphaOnly != 0) {
    // Alpha-only textures use R8 format in Metal. The sampler returns (r, 0, 0, 1).
    // Use .r to get the actual alpha value.
    outputCoverage = vec4(texColor.r);
  } else {
    // Color texture (e.g. color emoji): extract color and coverage
    outputColor = clamp(vec4(texColor.rgb / texColor.a, 1.0), 0.0, 1.0);
    outputCoverage = vec4(texColor.a);
  }

#define TGFX_INITIAL_COVERAGE outputCoverage
#define TGFX_COVERAGE_SRC_COLOR outputColor
#include "coverage_output.inc"
#include "xp_output.inc"
}
