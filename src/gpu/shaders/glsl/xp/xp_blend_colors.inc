// PorterDuff blend math shared by the xfer processors and the fused pointwise-chain kernel.
#pragma tgfx_spec_reader

// xpBlendColors(S, D, mode) blends two premultiplied colors with a runtime-selected mode. This
// file has its own include guard so kernels that need the blend math without the dst-texture XP
// machinery (the chain interpreter) can include it directly.
//
// The heavy modes (Overlay, ColorDodge, ColorBurn, HardLight, SoftLight, and the HSL family) are
// written as branch-free vector selections of the runtime's per-component formulas
// (GLSLBlend.cpp): each lane evaluates every arm and mix() with a bvec picks the arm the scalar
// branch would have taken. mix() with a boolean vector is a true component-wise select (SPIR-V
// OpSelect), so unselected lanes are discarded bit-for-bit. Division denominators are sanitized
// (zero replaced with one) only on lanes whose result the select discards, keeping every
// evaluated lane finite; the per-lane arithmetic mirrors the original expression trees exactly,
// so selected lanes produce bit-identical results.

#include "fn/xp/xpLuminance.inc"
#include "fn/xp/xpSaturation.inc"
#include "fn/xp/xpSetLuminance.inc"
#include "fn/xp/xpSetSaturation.inc"
#include "fn/xp/xpBlend_Clear.inc"
#include "fn/xp/xpBlend_Src.inc"
#include "fn/xp/xpBlend_Dst.inc"
#include "fn/xp/xpBlend_SrcOver.inc"
#include "fn/xp/xpBlend_DstOver.inc"
#include "fn/xp/xpBlend_SrcIn.inc"
#include "fn/xp/xpBlend_DstIn.inc"
#include "fn/xp/xpBlend_SrcOut.inc"
#include "fn/xp/xpBlend_DstOut.inc"
#include "fn/xp/xpBlend_SrcATop.inc"
#include "fn/xp/xpBlend_DstATop.inc"
#include "fn/xp/xpBlend_Xor.inc"
#include "fn/xp/xpBlend_PlusLighter.inc"
#include "fn/xp/xpBlend_Modulate.inc"
#include "fn/xp/xpBlend_Screen.inc"
#include "fn/xp/xpBlend_Overlay.inc"
#include "fn/xp/xpBlend_Darken.inc"
#include "fn/xp/xpBlend_Lighten.inc"
#include "fn/xp/xpBlend_ColorDodge.inc"
#include "fn/xp/xpBlend_ColorBurn.inc"
#include "fn/xp/xpBlend_HardLight.inc"
#include "fn/xp/xpBlend_SoftLight.inc"
#include "fn/xp/xpBlend_Difference.inc"
#include "fn/xp/xpBlend_Exclusion.inc"
#include "fn/xp/xpBlend_Multiply.inc"
#include "fn/xp/xpBlend_Hue.inc"
#include "fn/xp/xpBlend_Saturation.inc"
#include "fn/xp/xpBlend_Color.inc"
#include "fn/xp/xpBlend_Luminosity.inc"
#include "fn/xp/xpBlend_PlusDarker.inc"
#include "fn/xp/xpBlend_Clear_coverage.inc"
#include "fn/xp/xpBlend_Src_coverage.inc"
#include "fn/xp/xpBlend_Dst_coverage.inc"
#include "fn/xp/xpBlend_SrcOver_coverage.inc"
#include "fn/xp/xpBlend_DstOver_coverage.inc"
#include "fn/xp/xpBlend_SrcIn_coverage.inc"
#include "fn/xp/xpBlend_DstIn_coverage.inc"
#include "fn/xp/xpBlend_SrcOut_coverage.inc"
#include "fn/xp/xpBlend_DstOut_coverage.inc"
#include "fn/xp/xpBlend_SrcATop_coverage.inc"
#include "fn/xp/xpBlend_DstATop_coverage.inc"
#include "fn/xp/xpBlend_Xor_coverage.inc"
#include "fn/xp/xpBlend_PlusLighter_coverage.inc"
#include "fn/xp/xpBlend_Modulate_coverage.inc"
#include "fn/xp/xpBlend_Screen_coverage.inc"

vec4 xpBlendColors(vec4 S, vec4 D, int mode) {
  vec4 dstSrcAlpha = D * S.a;
  vec4 srcDstAlpha = S * D.a;
  if (mode == 0) {
    return xpBlend_Clear(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 1) {
    return xpBlend_Src(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 2) {
    return xpBlend_Dst(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 3) {
    return xpBlend_SrcOver(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 4) {
    return xpBlend_DstOver(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 5) {
    return xpBlend_SrcIn(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 6) {
    return xpBlend_DstIn(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 7) {
    return xpBlend_SrcOut(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 8) {
    return xpBlend_DstOut(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 9) {
    return xpBlend_SrcATop(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 10) {
    return xpBlend_DstATop(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 11) {
    return xpBlend_Xor(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 12) {
    return xpBlend_PlusLighter(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 13) {
    return xpBlend_Modulate(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 14) {
    return xpBlend_Screen(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 15) {
    return xpBlend_Overlay(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 16) {
    return xpBlend_Darken(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 17) {
    return xpBlend_Lighten(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 18) {
    return xpBlend_ColorDodge(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 19) {
    return xpBlend_ColorBurn(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 20) {
    return xpBlend_HardLight(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 21) {
    return xpBlend_SoftLight(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 22) {
    return xpBlend_Difference(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 23) {
    return xpBlend_Exclusion(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 24) {
    return xpBlend_Multiply(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 25) {
    return xpBlend_Hue(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 26) {
    return xpBlend_Saturation(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 27) {
    return xpBlend_Color(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 28) {
    return xpBlend_Luminosity(S, D, dstSrcAlpha, srcDstAlpha);
  }
  if (mode == 29) {
    return xpBlend_PlusDarker(S, D, dstSrcAlpha, srcDstAlpha);
  }
  return S;
}

#define XP_COMPOSITE_WITH_COVERAGE(ADVANCED_BLEND) \
  if (mode == 0) { \
    return xpBlend_Clear_coverage(S, D, C); \
  } \
  if (mode == 1) { \
    return xpBlend_Src_coverage(S, D, C); \
  } \
  if (mode == 2) { \
    return xpBlend_Dst_coverage(S, D, C); \
  } \
  if (mode == 3) { \
    return xpBlend_SrcOver_coverage(S, D, C); \
  } \
  if (mode == 4) { \
    return xpBlend_DstOver_coverage(S, D, C); \
  } \
  if (mode == 5) { \
    return xpBlend_SrcIn_coverage(S, D, C); \
  } \
  if (mode == 6) { \
    return xpBlend_DstIn_coverage(S, D, C); \
  } \
  if (mode == 7) { \
    return xpBlend_SrcOut_coverage(S, D, C); \
  } \
  if (mode == 8) { \
    return xpBlend_DstOut_coverage(S, D, C); \
  } \
  if (mode == 9) { \
    return xpBlend_SrcATop_coverage(S, D, C); \
  } \
  if (mode == 10) { \
    return xpBlend_DstATop_coverage(S, D, C); \
  } \
  if (mode == 11) { \
    return xpBlend_Xor_coverage(S, D, C); \
  } \
  if (mode == 12) { \
    return xpBlend_PlusLighter_coverage(S, D, C); \
  } \
  if (mode == 13) { \
    return xpBlend_Modulate_coverage(S, D, C); \
  } \
  if (mode == 14) { \
    return xpBlend_Screen_coverage(S, D, C); \
  } \
  vec4 blended = ADVANCED_BLEND; \
  return C * blended + (vec4(1.0) - C) * D;

#if HAS_XP >= 1
vec4 xpBlendWithCoverage(vec4 S, vec4 D, vec4 C, int mode) {
  XP_COMPOSITE_WITH_COVERAGE(xpBlendColors(S, D, mode))
}
#endif

