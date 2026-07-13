// QuadTextureFillShader fragment shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + TextureEffect + EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (frag): HAS_YUV, ALPHA_ONLY, HAS_RGBAAA, HAS_SUBSET, HAS_COVERAGE,
//                                HAS_COLOR, HAS_XP
// Note: HAS_YUV is always 0 at runtime — YUV textures fall back to ProgramBuilder.
// Vertex-driven varyings are controlled by vert permutation dimensions (HAS_COVERAGE, HAS_COLOR,
// HAS_SUBSET) which are communicated via matching varying declarations.
#version 450

#ifndef HAS_YUV
#define HAS_YUV 0
#endif
#ifndef ALPHA_ONLY
#define ALPHA_ONLY 0
#endif
#ifndef HAS_RGBAAA
#define HAS_RGBAAA 0
#endif
#ifndef HAS_SUBSET
#define HAS_SUBSET 0
#endif

// These are driven by vertex shader permutation but the fragment shader must declare matching
// varyings. The build tool defines them for each (vert, frag) pair.
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_COLOR
#define HAS_COLOR 0
#endif
#ifndef HAS_UV_PERSPECTIVE
#define HAS_UV_PERSPECTIVE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

#if !HAS_COLOR || HAS_SUBSET || HAS_RGBAAA || HAS_XP
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#if !HAS_COLOR
  vec4 Color;
#endif
#if HAS_SUBSET
  vec4 Subset;
#endif
#if HAS_RGBAAA
  vec2 AlphaStart;
#endif
#if HAS_XP
  vec2 DstTextureUpperLeft;
  vec2 DstTextureCoordScale;
  int XPBlendMode;
#endif
};
#endif

#if HAS_UV_PERSPECTIVE
layout(location = 0) in vec3 TransformedCoords_0;
#else
layout(location = 0) in vec2 TransformedCoords_0;
#endif

#if HAS_COVERAGE
layout(location = 1) in float vCoverage;
#endif

#if HAS_COLOR
layout(location = 2) in vec4 vColor;
#endif

#if HAS_SUBSET
layout(location = 3) in vec4 vTexSubset;
#endif

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COLOR
  vec4 outputColor = vColor;
#else
  vec4 outputColor = Color;
#endif

#if HAS_UV_PERSPECTIVE
  highp vec2 texCoord = TransformedCoords_0.xy / TransformedCoords_0.z;
#else
  highp vec2 texCoord = TransformedCoords_0;
#endif
  highp vec2 finalCoord = texCoord;

#if HAS_SUBSET
  finalCoord = clamp(finalCoord, vTexSubset.xy, vTexSubset.zw);
  finalCoord = clamp(finalCoord, Subset.xy, Subset.zw);
#endif

  vec4 color = texture(TextureSampler_0, finalCoord);

#if ALPHA_ONLY
  // Alpha-only textures use R8 format in Metal. The sampler returns (r, 0, 0, 1).
  // Replicate .r to all channels to get the actual alpha value.
  color = vec4(color.r);
#endif

#if HAS_RGBAAA
  color = clamp(color, 0.0, 1.0);
  highp vec2 alphaCoord = finalCoord + AlphaStart;
  vec4 alpha = texture(TextureSampler_0, alphaCoord);
  alpha = clamp(alpha, 0.0, 1.0);
  color = vec4(color.rgb * alpha.r, alpha.r);
#endif

#if ALPHA_ONLY
  color = color.a * outputColor;
#else
  color = color * outputColor.a;
#endif

#if HAS_XP
  #if HAS_COVERAGE
  fragColor = applyPorterDuffXP(color, vec4(vCoverage));
  #else
  fragColor = applyPorterDuffXP(color, vec4(1.0));
  #endif
#else
  // Apply coverage modulation.
  #if HAS_COVERAGE
  fragColor = color * vCoverage;
  #else
  fragColor = color;
  #endif
#endif
}
