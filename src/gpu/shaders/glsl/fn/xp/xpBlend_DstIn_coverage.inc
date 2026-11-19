//@function xpBlend_DstIn_coverage
vec4 xpBlend_DstIn_coverage(vec4 S, vec4 D, vec4 C) {
  return clamp(D - (1.0 - S.a) * C * D, 0.0, 1.0);
}
