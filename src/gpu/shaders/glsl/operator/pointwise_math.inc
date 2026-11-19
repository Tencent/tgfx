// Explicit-parameter pointwise math shared by the plain evaluator (pointwise_op.inc, scalar
// uniforms) and the chain evaluator (pointwise_chain_eval.inc, packed selectors and slot
// arrays). Each function carries exactly the operator's math, byte-identical to the previously
// duplicated bodies; only the parameter sourcing differs per caller. Keep this file free of
// uniform names, samplers, and selector decoding so both evaluators (and any future one) can
// include it.
#ifndef TGFX_POINTWISE_MATH_INC
#define TGFX_POINTWISE_MATH_INC

// This file reads the specialization surface (the per-operator gates below).
#pragma tgfx_spec_reader

#include "operator/transfer_function.inc"

// The chain slot's straight-line form lives in ops/ColorSpaceXform.glsl (macro-free, the pack's
// copy); the plain evaluator below still spells the stages through this apply macro.
#include "ops/ColorSpaceXform.glsl"

// ColorSpaceXform stage flags (the CSFlags / ChainFlags bit set), shared by both evaluators.
#define CS_UNPREMUL 1
#define CS_LINEARIZE 2
#define CS_GAMUT 4
#define CS_ENCODE 8
#define CS_PREMUL 16
#define CS_SRC_OOTF 32
#define CS_DST_OOTF 64

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

// Each operator's math is only reachable from a chain whose slots carry that operator: the
// dispatch branch holds a constant `op` under specialization, so an absent operator's call sites
// fold away and the runtime splices the body out. The bodies live in ops/ as pure fragments; the
// gates stay here, on the interpreter's specialization surface.
#pragma tgfx_spec_gate ColorMatrixMath : (SPEC_UsedOps() & (1 << OP_COLOR_MATRIX)) != 0
#include "ops/ColorMatrix.glsl"

#pragma tgfx_spec_gate LumaMath : (SPEC_UsedOps() & (1 << OP_LUMA)) != 0
#include "ops/Luma.glsl"

#pragma tgfx_spec_gate AlphaThresholdMath : (SPEC_UsedOps() & (1 << OP_ALPHA_THRESHOLD)) != 0
#include "ops/AlphaThreshold.glsl"

// 0 = Ignore (replace), 1 = ModulateRGBA, 2 = ModulateA.
#pragma tgfx_spec_gate ConstColorMath : (SPEC_UsedOps() & (1 << OP_CONST_COLOR)) != 0
#include "ops/ConstColor.glsl"

// Premultiplied-in/premultiplied-out color space transform applied to the lvalue COLOR in place;
// the flags select which stages run, in the fixed order unpremul, linearize, source OOTF, gamut,
// destination OOTF, encode, premul. A macro rather than a function so each evaluator keeps
// reading its own uniforms in place: an eleven-parameter function added about 150 bytes to every
// chain variant. Keep comments out of the body, a line comment would swallow the continuation.
#endif  // TGFX_POINTWISE_MATH_INC
