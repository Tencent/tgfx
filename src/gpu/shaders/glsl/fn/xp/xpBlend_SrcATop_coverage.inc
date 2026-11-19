//@function xpBlend_SrcATop_coverage
vec4 xpBlend_SrcATop_coverage(vec4 S, vec4 D, vec4 C) {
  return clamp(S * C * D.a + D * (1.0 - (S * C).a), 0.0, 1.0);
}
