// TextureColorMatrixShader fragment shader
// Processor layout: DefaultGeometryProcessor(_P0) + TextureEffect(_P1) + ColorMatrixFP(_P2) + EmptyXferProcessor(_P3)
// Permutation dimensions (injected by build tool as #define 0/1):
//   ALPHA_ONLY, HAS_RGBAAA, HAS_SUBSET
#version 450

#ifndef ALPHA_ONLY
#define ALPHA_ONLY 0
#endif
#ifndef HAS_RGBAAA
#define HAS_RGBAAA 0
#endif
#ifndef HAS_SUBSET
#define HAS_SUBSET 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color_P0;
#if HAS_SUBSET
  vec4 Subset_P1;
#endif
#if HAS_RGBAAA
  vec2 AlphaStart_P1;
#endif
  mat4 ColorMatrix_P2;
  vec4 ColorVector_P2;
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0_P1;

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 outputColor_P0 = Color_P0;
  highp vec2 texCoord = TransformedCoords_0;
  highp vec2 finalCoord = texCoord;

#if HAS_SUBSET
  finalCoord = clamp(finalCoord, Subset_P1.xy, Subset_P1.zw);
#endif

  vec4 texColor = texture(TextureSampler_0_P1, finalCoord);

#if HAS_RGBAAA
  texColor = clamp(texColor, 0.0, 1.0);
  highp vec2 alphaCoord = finalCoord + AlphaStart_P1;
  vec4 alpha = texture(TextureSampler_0_P1, alphaCoord);
  alpha = clamp(alpha, 0.0, 1.0);
  texColor = vec4(texColor.rgb * alpha.r, alpha.r);
#endif

  // TextureEffect post-processing: alpha multiply
#if ALPHA_ONLY
  texColor = texColor.a * outputColor_P0;
#else
  texColor = texColor * outputColor_P0.a;
#endif

  // ColorMatrixFragmentProcessor: apply color matrix to the texture color
  // Unpremultiply
  texColor = vec4(texColor.rgb / max(texColor.a, 9.9999997473787516e-05), texColor.a);
  // Apply matrix + vector, clamp to [0,1]
  texColor = clamp(ColorMatrix_P2 * texColor + ColorVector_P2, 0.0, 1.0);
  // Re-premultiply
  texColor.rgb *= texColor.a;

  // EmptyXferProcessor — passthrough
  fragColor = texColor;
}
