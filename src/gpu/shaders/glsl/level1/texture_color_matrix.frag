// TextureColorMatrixShader fragment shader
// Processor layout: DefaultGeometryProcessor + TextureEffect + ColorMatrixFP + EmptyXferProcessor
// Permutation dimensions (injected by build tool as #define 0/1):
//   HAS_SUBSET, HAS_XP
// ALPHA_ONLY and HAS_RGBAAA are runtime uniforms (AlphaOnly / HasRgbaaa), not compile-time
// permutations: both are pure fragment math, so folding them into uniform branches shrinks the
// variant count. This mirrors TextureFillShader.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  // Always declared: a draw without a subset uploads the full texture bounds, making the clamp a
  // no-op, so subset handling needs no compile-time dimension.
  vec4 Subset;
  // RGBAAA dual-plane alpha offset: always declared, only read when HasRgbaaa != 0 (uniform branch).
  vec2 AlphaStart;
  mat4 ColorMatrix;
  vec4 ColorVector;
#include "xp_uniforms.inc"
  int AlphaOnly;
  int HasRgbaaa;
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

  finalCoord = clamp(finalCoord, Subset.xy, Subset.zw);

  vec4 texColor = texture(TextureSampler_0, finalCoord);

  if (AlphaOnly != 0) {
    // Alpha-only textures use R8 format in Metal. Use .r to get the actual alpha value.
    texColor = vec4(texColor.r);
  }

  if (HasRgbaaa != 0) {
    texColor = clamp(texColor, 0.0, 1.0);
    highp vec2 alphaCoord = finalCoord + AlphaStart;
    vec4 alpha = texture(TextureSampler_0, alphaCoord);
    alpha = clamp(alpha, 0.0, 1.0);
    texColor = vec4(texColor.rgb * alpha.r, alpha.r);
  }

  // TextureEffect post-processing: alpha multiply
  if (AlphaOnly != 0) {
    texColor = texColor.a * outputColor;
  } else {
    texColor = texColor * outputColor.a;
  }

  // ColorMatrixFragmentProcessor: apply color matrix to the texture color
  // Unpremultiply
  texColor = vec4(texColor.rgb / max(texColor.a, 9.9999997473787516e-05), texColor.a);
  // Apply matrix + vector, clamp to [0,1]
  texColor = clamp(ColorMatrix * texColor + ColorVector, 0.0, 1.0);
  // Re-premultiply
  texColor.rgb *= texColor.a;

#define TGFX_XP_SRC_COLOR texColor
#include "xp_output.inc"
}
