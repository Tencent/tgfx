// Explicit-parameter pointwise math shared by the plain evaluator (pointwise_op.inc, scalar
// uniforms) and the chain evaluator (pointwise_chain_eval.inc, packed selectors and slot
// arrays). Each function carries exactly the operator's math, byte-identical to the previously
// duplicated bodies; only the parameter sourcing differs per caller. Keep this file free of
// uniform names, samplers, and selector decoding so both evaluators (and any future one) can
// include it.
#ifndef TGFX_POINTWISE_MATH_INC
#define TGFX_POINTWISE_MATH_INC

#include "operator/transfer_function.inc"

// ColorSpaceXform stage flags (the CSFlags / ChainCSFlags bit set), shared by both evaluators.
#define CS_UNPREMUL 1
#define CS_LINEARIZE 2
#define CS_GAMUT 4
#define CS_ENCODE 8
#define CS_PREMUL 16
#define CS_SRC_OOTF 32
#define CS_DST_OOTF 64

vec4 pmApplyColorMatrix(vec4 color, mat4 matrix, vec4 vector) {
  // Unpremultiply, apply matrix + vector, clamp, re-premultiply.
  color = vec4(color.rgb / max(color.a, 9.9999997473787516e-05), color.a);
  color = clamp(matrix * color + vector, 0.0, 1.0);
  color.rgb *= color.a;
  return color;
}

vec4 pmApplyLuma(vec4 color, vec3 lumaCoefficients) {
  return vec4(dot(color.rgb, lumaCoefficients));
}

vec4 pmApplyAlphaThreshold(vec4 color, float threshold) {
  vec4 result = vec4(0.0);
  if (color.a > 0.0) {
    result.rgb = color.rgb / color.a;
    result.a = step(threshold, color.a);
    result = clamp(result, 0.0, 1.0);
  }
  return result;
}

// 0 = Ignore (replace), 1 = ModulateRGBA, 2 = ModulateA.
vec4 pmApplyConstColor(vec4 constColor, vec4 inputColor, int inputMode) {
  if (inputMode == 1) {
    return constColor * inputColor;
  }
  if (inputMode == 2) {
    return constColor * inputColor.a;
  }
  return constColor;
}

// Premultiplied-in/premultiplied-out color space transform applied to the lvalue COLOR in place;
// the flags select which stages run, in the fixed order unpremul, linearize, source OOTF, gamut,
// destination OOTF, encode, premul. A macro rather than a function so each evaluator keeps
// reading its own uniforms in place: an eleven-parameter function added about 150 bytes to every
// chain variant. Keep comments out of the body, a line comment would swallow the continuation.
#define PM_APPLY_COLOR_SPACE_XFORM(COLOR, FLAGS, SRC_TF0, SRC_TF1, SRC_TF_TYPE, SRC_OOTF, GAMUT, \
                                   DST_OOTF, DST_TF0, DST_TF1, DST_TF_TYPE)                      \
  if (((FLAGS) & CS_UNPREMUL) != 0) {                                                            \
    float csAlpha = COLOR.a;                                                                     \
    COLOR = (csAlpha > 0.0) ? vec4(COLOR.rgb / csAlpha, csAlpha) : vec4(0.0);                    \
  }                                                                                              \
  if (((FLAGS) & CS_LINEARIZE) != 0) {                                                           \
    COLOR.rgb = transferTF(COLOR.rgb, SRC_TF0, SRC_TF1, SRC_TF_TYPE);                            \
  }                                                                                              \
  if (((FLAGS) & CS_SRC_OOTF) != 0) {                                                            \
    float csSrcY = dot(COLOR.rgb, SRC_OOTF.rgb);                                                 \
    COLOR.rgb *= sign(csSrcY) * pow(abs(csSrcY), SRC_OOTF.a);                                    \
  }                                                                                              \
  if (((FLAGS) & CS_GAMUT) != 0) {                                                               \
    COLOR.rgb = GAMUT * COLOR.rgb;                                                               \
  }                                                                                              \
  if (((FLAGS) & CS_DST_OOTF) != 0) {                                                            \
    float csDstY = dot(COLOR.rgb, DST_OOTF.rgb);                                                 \
    COLOR.rgb *= sign(csDstY) * pow(abs(csDstY), DST_OOTF.a);                                    \
  }                                                                                              \
  if (((FLAGS) & CS_ENCODE) != 0) {                                                              \
    COLOR.rgb = transferTF(COLOR.rgb, DST_TF0, DST_TF1, DST_TF_TYPE);                            \
  }                                                                                              \
  if (((FLAGS) & CS_PREMUL) != 0) {                                                              \
    COLOR.rgb *= COLOR.a;                                                                        \
  }

#endif  // TGFX_POINTWISE_MATH_INC
