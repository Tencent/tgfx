// TexturedColorSpaceXformShader fragment shader
// Used in EffectDecomposer 2-pass pipeline: samples intermediate texture + color space xform.
// Processor layout: ComposeFragmentProcessor(TextureEffect, ColorSpaceXformEffect)
// The color-space pipeline steps are selected at runtime through the CSFlags bitmask uniform
// (mirrors ColorSpaceXformSteps::Flags::mask()) instead of compile-time dimensions.
// Permutation dimensions (frag):
//   HAS_SUBSET (bool): whether TextureEffect needs subset clamping
// Runtime uniforms:
//   CSFlags (int): bit 0 unpremul, 1 linearize, 2 gamut, 3 encode, 4 premul, 5 srcOOTF, 6 dstOOTF
//   SrcTFType / DstTFType (int): 0=sRGBish, 1=PQish, 2=HLGish, 3=HLGinvish
#version 450

#ifndef HAS_SUBSET
#define HAS_SUBSET 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

#define CS_UNPREMUL 1
#define CS_LINEARIZE 2
#define CS_GAMUT 4
#define CS_ENCODE 8
#define CS_PREMUL 16
#define CS_SRC_OOTF 32
#define CS_DST_OOTF 64

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
#if HAS_SUBSET
  vec4 Subset;
#endif
  int CSFlags;
  vec4 SrcTF0;
  vec4 SrcTF1;
  int SrcTFType;
  vec4 SrcOOTF;
  mat3 ColorXform;
  vec4 DstOOTF;
  vec4 DstTF0;
  vec4 DstTF1;
  int DstTFType;
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

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

  if ((CSFlags & CS_UNPREMUL) != 0) {
    float alpha = color.a;
    color = (alpha > 0.0) ? vec4(color.rgb / alpha, alpha) : vec4(0.0);
  }

  if ((CSFlags & CS_LINEARIZE) != 0) {
    color.r = src_tf(color.r);
    color.g = src_tf(color.g);
    color.b = src_tf(color.b);
  }

  if ((CSFlags & CS_SRC_OOTF) != 0) {
    float srcY = dot(color.rgb, SrcOOTF.rgb);
    color.rgb *= sign(srcY) * pow(abs(srcY), SrcOOTF.a);
  }

  if ((CSFlags & CS_GAMUT) != 0) {
    color.rgb = ColorXform * color.rgb;
  }

  if ((CSFlags & CS_DST_OOTF) != 0) {
    float dstY = dot(color.rgb, DstOOTF.rgb);
    color.rgb *= sign(dstY) * pow(abs(dstY), DstOOTF.a);
  }

  if ((CSFlags & CS_ENCODE) != 0) {
    color.r = dst_tf(color.r);
    color.g = dst_tf(color.g);
    color.b = dst_tf(color.b);
  }

  if ((CSFlags & CS_PREMUL) != 0) {
    color.rgb *= color.a;
  }

#define TGFX_XP_SRC_COLOR color
#include "xp_output.inc"
}
