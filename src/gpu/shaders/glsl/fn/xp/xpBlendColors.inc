//@function xpBlendColors
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
