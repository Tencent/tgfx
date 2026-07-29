// TextureFillShader fragment shader (non-YUV path only)
// Processor layout: DefaultGeometryProcessor() + TextureEffect() + [coverage FP] + EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (injected by build tool as #define 0/1):
//   HAS_SUBSET, HAS_XP, HAS_COVERAGE
// ALPHA_ONLY and HAS_RGBAAA are runtime uniforms (AlphaOnly / HasRgbaaa), not compile-time
// permutations: both are pure fragment math (alpha-only replicates .r; RGBAAA adds one
// coherent-branch alpha sample), so folding them into uniform branches shrinks the variant count.
// This mirrors QuadTextureFillShader.
// Note: HAS_YUV is always 0 at runtime — YUV textures fall back to ProgramBuilder.
#version 450

#ifndef HAS_SUBSET
#define HAS_SUBSET 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
#if HAS_SUBSET
  vec4 Subset;
#endif
  // RGBAAA dual-plane alpha offset: always declared, only read when HasRgbaaa != 0 (uniform branch).
  vec2 AlphaStart;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
  int AlphaOnly;
  int HasRgbaaa;
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

#if HAS_COVERAGE == 2
layout(set = 1, binding = 1) uniform sampler2D MaskTextureSampler;
  #define XP_DST_TEX_BINDING 2
#else
  #define XP_DST_TEX_BINDING 1
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 outputColor = Color;
  highp vec2 texCoord = TransformedCoords_0;
  highp vec2 finalCoord = texCoord;

#if HAS_SUBSET
  finalCoord = clamp(finalCoord, Subset.xy, Subset.zw);
#endif

  vec4 color = texture(TextureSampler_0, finalCoord);

  if (AlphaOnly != 0) {
    // Alpha-only textures use R8 format in Metal. The sampler returns (r, 0, 0, 1).
    // Use .r (replicated to all channels) to get the actual alpha value.
    color = vec4(color.r);
  }

  if (HasRgbaaa != 0) {
    color = clamp(color, 0.0, 1.0);
    highp vec2 alphaCoord = finalCoord + AlphaStart;
    vec4 alpha = texture(TextureSampler_0, alphaCoord);
    alpha = clamp(alpha, 0.0, 1.0);
    color = vec4(color.rgb * alpha.r, alpha.r);
  }

  // Post-processing: alpha multiply
  if (AlphaOnly != 0) {
    color = color.a * outputColor;
  } else {
    color = color * outputColor.a;
  }

#define TGFX_COVERAGE_SRC_COLOR color
#include "coverage_output.inc"
#include "xp_output.inc"
}
