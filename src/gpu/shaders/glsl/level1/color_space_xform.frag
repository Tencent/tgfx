// ColorSpaceXformShader fragment shader
// Permutation dimensions (frag):
//   UNPREMUL, LINEARIZE, SRC_OOTF, GAMUT_TRANSFORM, DST_OOTF, ENCODE, PREMUL (bool)
//   SRC_TF_TYPE (0=sRGBish, 1=PQish, 2=HLGish, 3=HLGinvish)
//   DST_TF_TYPE (0=sRGBish, 1=PQish, 2=HLGish, 3=HLGinvish)
#version 450

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
#ifndef SRC_TF_TYPE
#define SRC_TF_TYPE 0
#endif
#ifndef DST_TF_TYPE
#define DST_TF_TYPE 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
#if LINEARIZE
  vec4 SrcTF0;
  vec4 SrcTF1;
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
#endif
};

layout(location = 0) out vec4 fragColor;

#if LINEARIZE
float src_tf(float x) {
  float s = sign(x);
  x = abs(x);
#if SRC_TF_TYPE == 0
  // sRGBish: f(x) = (Cx + F) if x < D, else (Ax + B)^G + E
  float G = SrcTF0[0], A = SrcTF0[1], B = SrcTF0[2], C = SrcTF0[3];
  float D = SrcTF1[0], E = SrcTF1[1], F = SrcTF1[2];
  x = (x < D) ? (C * x + F) : (pow(A * x + B, G) + E);
#elif SRC_TF_TYPE == 1
  // PQish: f(x) = ((A + B * x^C) / (D + E * x^C))^F
  float A = SrcTF0[1], B = SrcTF0[2], C = SrcTF0[3];
  float D = SrcTF1[0], E = SrcTF1[1], F = SrcTF1[2];
  float xC = pow(x, C);
  x = pow(max(A + B * xC, 0.0) / (D + E * xC), F);
#elif SRC_TF_TYPE == 2
  // HLGish: f(x) = (F+1) * ((xA)^B if xA<=1, else exp((x-E)*C)+D)
  float A = SrcTF0[1], B = SrcTF0[2], C = SrcTF0[3];
  float D = SrcTF1[0], E = SrcTF1[1], F = SrcTF1[2];
  x = (x * A <= 1.0) ? pow(x * A, B) : exp((x - E) * C) + D;
  x *= (F + 1.0);
#elif SRC_TF_TYPE == 3
  // HLGinvish: f(x) = A*(x/(F+1))^B if x/(F+1)<=1, else C*ln(x/(F+1)-D)+E
  float A = SrcTF0[1], B = SrcTF0[2], C = SrcTF0[3];
  float D = SrcTF1[0], E = SrcTF1[1], F = SrcTF1[2];
  x /= (F + 1.0);
  x = (x <= 1.0) ? A * pow(x, B) : C * log(x - D) + E;
#endif
  return s * x;
}
#endif

#if ENCODE
float dst_tf(float x) {
  float s = sign(x);
  x = abs(x);
#if DST_TF_TYPE == 0
  float G = DstTF0[0], A = DstTF0[1], B = DstTF0[2], C = DstTF0[3];
  float D = DstTF1[0], E = DstTF1[1], F = DstTF1[2];
  x = (x < D) ? (C * x + F) : (pow(A * x + B, G) + E);
#elif DST_TF_TYPE == 1
  float A = DstTF0[1], B = DstTF0[2], C = DstTF0[3];
  float D = DstTF1[0], E = DstTF1[1], F = DstTF1[2];
  float xC = pow(x, C);
  x = pow(max(A + B * xC, 0.0) / (D + E * xC), F);
#elif DST_TF_TYPE == 2
  float A = DstTF0[1], B = DstTF0[2], C = DstTF0[3];
  float D = DstTF1[0], E = DstTF1[1], F = DstTF1[2];
  x = (x * A <= 1.0) ? pow(x * A, B) : exp((x - E) * C) + D;
  x *= (F + 1.0);
#elif DST_TF_TYPE == 3
  float A = DstTF0[1], B = DstTF0[2], C = DstTF0[3];
  float D = DstTF1[0], E = DstTF1[1], F = DstTF1[2];
  x /= (F + 1.0);
  x = (x <= 1.0) ? A * pow(x, B) : C * log(x - D) + E;
#endif
  return s * x;
}
#endif

void main() {
  vec4 color = Color;

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

  fragColor = color;
}
