//@function xpBlend_DstATop_coverage
vec4 xpBlend_DstATop_coverage(vec4 S, vec4 D, vec4 C) {
  return clamp(S * C * (1.0 - D.a) + D * (vec4(1.0) - (1.0 - S.a) * C), 0.0, 1.0);
}
