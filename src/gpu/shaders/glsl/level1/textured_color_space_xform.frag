// TexturedColorSpaceXformShader fragment shader
// Used in EffectDecomposer 2-pass pipeline: samples intermediate texture + color space xform.
// Processor layout: ComposeFragmentProcessor(TextureEffect, ColorSpaceXformEffect)
// Permutation dimensions (frag):
//   HAS_SUBSET (bool): whether TextureEffect needs subset clamping
//   UNPREMUL, LINEARIZE, SRC_OOTF, GAMUT_TRANSFORM, DST_OOTF, ENCODE, PREMUL (bool)
// Runtime uniforms:
//   SrcTFType (int): 0=sRGBish, 1=PQish, 2=HLGish, 3=HLGinvish
//   DstTFType (int): 0=sRGBish, 1=PQish, 2=HLGish, 3=HLGinvish
#version 450

#ifndef HAS_SUBSET
#define HAS_SUBSET 0
#endif
#ifndef UNPREMUL
#define UNPREMUL 0
#endif
#ifndef LINEARIZE
#define LINEARIZE 0
#endif
#ifndef SRC_OOTF
#define SRC_OOTF 0
#endif
#ifndef GAMUT_TRANSFORM
#define GAMUT_TRANSFORM 0
#endif
#ifndef DST_OOTF
#define DST_OOTF 0
#endif
#ifndef ENCODE
#define ENCODE 0
#endif
#ifndef PREMUL
#define PREMUL 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
#if HAS_SUBSET
  vec4 Subset;
#endif
#if LINEARIZE
  vec4 SrcTF0;
  vec4 SrcTF1;
  int SrcTFType;
#endif
#if SRC_OOTF
  vec4 SrcOOTF;
#endif
#if GAMUT_TRANSFORM
  mat3 ColorXform;
#endif
#if DST_OOTF
  vec4 DstOOTF;
#endif
#if ENCODE
  vec4 DstTF0;
  vec4 DstTF1;
  int DstTFType;
#endif
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

#if LINEARIZE
float src_tf(float x) {
  float s = sign(x);
  x = abs(x);
  float A = SrcTF0[1], B = SrcTF0[2], C = SrcTF0[3];
  float D = SrcTF1[0], E = SrcTF1[1], F = SrcTF1[2];
  if (SrcTFType == 0) {
    float G = SrcTF0[0];
    x = (x < D) ? (C * x + F) : (pow(A * x + B, G) + E);
  } else if (SrcTFType == 1) {
    float xC = pow(x, C);
    x = pow(max(A + B * xC, 0.0) / (D + E * xC), F);
  } else if (SrcTFType == 2) {
    x = (x * A <= 1.0) ? pow(x * A, B) : exp((x - E) * C) + D;
    x *= (F + 1.0);
  } else {
    x /= (F + 1.0);
    x = (x <= 1.0) ? A * pow(x, B) : C * log(x - D) + E;
  }
  return s * x;
}
#endif

#if ENCODE
float dst_tf(float x) {
  float s = sign(x);
  x = abs(x);
  float A = DstTF0[1], B = DstTF0[2], C = DstTF0[3];
  float D = DstTF1[0], E = DstTF1[1], F = DstTF1[2];
  if (DstTFType == 0) {
    float G = DstTF0[0];
    x = (x < D) ? (C * x + F) : (pow(A * x + B, G) + E);
  } else if (DstTFType == 1) {
    float xC = pow(x, C);
    x = pow(max(A + B * xC, 0.0) / (D + E * xC), F);
  } else if (DstTFType == 2) {
    x = (x * A <= 1.0) ? pow(x * A, B) : exp((x - E) * C) + D;
    x *= (F + 1.0);
  } else {
    x /= (F + 1.0);
    x = (x <= 1.0) ? A * pow(x, B) : C * log(x - D) + E;
  }
  return s * x;
}
#endif

void main() {
  vec4 outputColor = Color;
  highp vec2 texCoord = TransformedCoords_0;
  highp vec2 finalCoord = texCoord;

#if HAS_SUBSET
  finalCoord = clamp(finalCoord, Subset.xy, Subset.zw);
#endif

  vec4 color = texture(TextureSampler_0, finalCoord);

  // TextureEffect post-processing: intermediate is never alpha-only or RGBAAA
  color = color * outputColor.a;

  // ColorSpaceXformEffect: transform the sampled color
#if UNPREMUL
  float alpha = color.a;
  color = (alpha > 0.0) ? vec4(color.rgb / alpha, alpha) : vec4(0.0);
#endif

#if LINEARIZE
  color.r = src_tf(color.r);
  color.g = src_tf(color.g);
  color.b = src_tf(color.b);
#endif

#if SRC_OOTF
  float srcY = dot(color.rgb, SrcOOTF.rgb);
  color.rgb *= sign(srcY) * pow(abs(srcY), SrcOOTF.a);
#endif

#if GAMUT_TRANSFORM
  color.rgb = ColorXform * color.rgb;
#endif

#if DST_OOTF
  float dstY = dot(color.rgb, DstOOTF.rgb);
  color.rgb *= sign(dstY) * pow(abs(dstY), DstOOTF.a);
#endif

#if ENCODE
  color.r = dst_tf(color.r);
  color.g = dst_tf(color.g);
  color.b = dst_tf(color.b);
#endif

#if PREMUL
  color.rgb *= color.a;
#endif

#define TGFX_XP_SRC_COLOR color
#include "xp_output.inc"
}
