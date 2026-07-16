// TexturedColorMatrixShader fragment shader (decompose version)
// Used in EffectDecomposer 2-pass pipeline: samples intermediate texture + color matrix.
// Processor layout: ComposeFragmentProcessor(TextureEffect, ColorMatrixFragmentProcessor)
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
  mat4 ColorMatrix;
  vec4 ColorVector;
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

  // ColorMatrixFragmentProcessor: apply color matrix
  // Unpremultiply
  color = vec4(color.rgb / max(color.a, 9.9999997473787516e-05), color.a);
  // Apply matrix + vector, clamp to [0,1]
  color = clamp(ColorMatrix * color + ColorVector, 0.0, 1.0);
  // Re-premultiply
  color.rgb *= color.a;

#if HAS_XP
  fragColor = applyPorterDuffXP(color, vec4(1.0));
#else
  fragColor = color;
#endif
}
