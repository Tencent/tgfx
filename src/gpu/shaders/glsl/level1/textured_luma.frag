// TexturedLumaShader fragment shader
// Used in EffectDecomposer 2-pass pipeline: samples intermediate texture + luma conversion.
// Processor layout: ComposeFragmentProcessor(TextureEffect, LumaFragmentProcessor)
// Permutation dimensions (frag): HAS_SUBSET (bool)
#version 450

#ifndef HAS_SUBSET
#define HAS_SUBSET 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
#if HAS_SUBSET
  vec4 Subset;
#endif
  float Kr;
  float Kg;
  float Kb;
#if HAS_XP
  vec2 DstTextureUpperLeft;
  vec2 DstTextureCoordScale;
  int XPBlendMode;
#endif
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

#define XP_DST_TEX_BINDING 1
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
#ifndef HAS_XP
#define HAS_XP 0
#endif

  vec4 color = texture(TextureSampler_0, finalCoord);

  // TextureEffect post-processing: intermediate is never alpha-only or RGBAAA
  color = color * outputColor.a;

  // LumaFragmentProcessor: convert to luminance
  float luma = dot(color.rgb, vec3(Kr, Kg, Kb));
#if HAS_XP
  fragColor = applyPorterDuffXP(vec4(luma), vec4(1.0));
#else
  fragColor = vec4(luma);
#endif
}
