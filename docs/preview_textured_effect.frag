// [PREVIEW - 不提交] TexturedEffectShader fragment shader
// 合并 4 个 L2-decompose shader: ColorMatrix / Luma / AlphaThreshold / ColorSpaceXform
// 骨架相同(采样中间纹理 + 一个 pointwise op + XP),仅 op 不同 → OpType uniform 分派。
// Processor layout: ComposeFragmentProcessor(TextureEffect, <pointwise op>)
// 变体: 4 shader × 12 → 1 shader × 12 (HAS_SUBSET × HAS_XP)
//
// OpType (runtime uniform): 0=ColorMatrix 1=Luma 2=AlphaThreshold 3=ColorSpaceXform
//   由各 pointwise FP 的 onSetData 通过 hasField("OpType") 设置自身常量。
#version 450

#ifndef HAS_SUBSET
#define HAS_SUBSET 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

#define OP_COLOR_MATRIX 0
#define OP_LUMA 1
#define OP_ALPHA_THRESHOLD 2
#define OP_COLOR_SPACE_XFORM 3

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
  int OpType;
  // --- ColorMatrix op ---
  mat4 ColorMatrix;
  vec4 ColorVector;
  // --- Luma op ---
  float Kr;
  float Kg;
  float Kb;
  // --- AlphaThreshold op ---
  float Threshold;
  // --- ColorSpaceXform op ---
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

// (CSX 专用) 传递函数,与 textured_color_space_xform.frag 逐字一致
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
  highp vec2 finalCoord = TransformedCoords_0;
#if HAS_SUBSET
  finalCoord = clamp(finalCoord, Subset.xy, Subset.zw);
#endif

  vec4 color = texture(TextureSampler_0, finalCoord);
  // 中间纹理不会是 alpha-only / RGBAAA
  color = color * outputColor.a;

  vec4 result;
  if (OpType == OP_COLOR_MATRIX) {
    color = vec4(color.rgb / max(color.a, 9.9999997473787516e-05), color.a);
    color = clamp(ColorMatrix * color + ColorVector, 0.0, 1.0);
    color.rgb *= color.a;
    result = color;
  } else if (OpType == OP_LUMA) {
    result = vec4(dot(color.rgb, vec3(Kr, Kg, Kb)));
  } else if (OpType == OP_ALPHA_THRESHOLD) {
    result = vec4(0.0);
    if (color.a > 0.0) {
      result.rgb = color.rgb / color.a;
      result.a = step(Threshold, color.a);
      result = clamp(result, 0.0, 1.0);
    }
  } else {  // OP_COLOR_SPACE_XFORM
    if ((CSFlags & CS_UNPREMUL) != 0) {
      float a = color.a;
      color = (a > 0.0) ? vec4(color.rgb / a, a) : vec4(0.0);
    }
    if ((CSFlags & CS_LINEARIZE) != 0) {
      color.r = src_tf(color.r); color.g = src_tf(color.g); color.b = src_tf(color.b);
    }
    if ((CSFlags & CS_SRC_OOTF) != 0) {
      float y = dot(color.rgb, SrcOOTF.rgb);
      color.rgb *= sign(y) * pow(abs(y), SrcOOTF.a);
    }
    if ((CSFlags & CS_GAMUT) != 0) { color.rgb = ColorXform * color.rgb; }
    if ((CSFlags & CS_DST_OOTF) != 0) {
      float y = dot(color.rgb, DstOOTF.rgb);
      color.rgb *= sign(y) * pow(abs(y), DstOOTF.a);
    }
    if ((CSFlags & CS_ENCODE) != 0) {
      color.r = dst_tf(color.r); color.g = dst_tf(color.g); color.b = dst_tf(color.b);
    }
    if ((CSFlags & CS_PREMUL) != 0) { color.rgb *= color.a; }
    result = color;
  }

#define TGFX_XP_SRC_COLOR result
#include "xp_output.inc"
}
