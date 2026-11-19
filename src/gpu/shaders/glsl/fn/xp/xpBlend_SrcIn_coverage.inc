//@function xpBlend_SrcIn_coverage
vec4 xpBlend_SrcIn_coverage(vec4 S, vec4 D, vec4 C) {
  return clamp(S * C * D.a + D * (1.0 - C.a), 0.0, 1.0);
}
