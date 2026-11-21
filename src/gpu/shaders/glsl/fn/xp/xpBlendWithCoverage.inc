// The coverage composite: the porter-duff modes (0-14) have dedicated coverage forms; the
// advanced modes fold through the plain composite below (the precomputed blended operand).
//@function xpBlendWithCoverage
vec4 xpBlendWithCoverage(vec4 S, vec4 D, vec4 C, int mode) {
  if (mode == 0) {
    return xpBlend_Clear_coverage(S, D, C);
  }
  if (mode == 1) {
    return xpBlend_Src_coverage(S, D, C);
  }
  if (mode == 2) {
    return xpBlend_Dst_coverage(S, D, C);
  }
  if (mode == 3) {
    return xpBlend_SrcOver_coverage(S, D, C);
  }
  if (mode == 4) {
    return xpBlend_DstOver_coverage(S, D, C);
  }
  if (mode == 5) {
    return xpBlend_SrcIn_coverage(S, D, C);
  }
  if (mode == 6) {
    return xpBlend_DstIn_coverage(S, D, C);
  }
  if (mode == 7) {
    return xpBlend_SrcOut_coverage(S, D, C);
  }
  if (mode == 8) {
    return xpBlend_DstOut_coverage(S, D, C);
  }
  if (mode == 9) {
    return xpBlend_SrcATop_coverage(S, D, C);
  }
  if (mode == 10) {
    return xpBlend_DstATop_coverage(S, D, C);
  }
  if (mode == 11) {
    return xpBlend_Xor_coverage(S, D, C);
  }
  if (mode == 12) {
    return xpBlend_PlusLighter_coverage(S, D, C);
  }
  if (mode == 13) {
    return xpBlend_Modulate_coverage(S, D, C);
  }
  if (mode == 14) {
    return xpBlend_Screen_coverage(S, D, C);
  }
  vec4 blended = xpBlendColors(S, D, mode);
  return C * blended + (vec4(1.0) - C) * D;
}
